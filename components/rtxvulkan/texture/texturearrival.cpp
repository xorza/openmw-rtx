#include "texturearrival.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

#include <volk.h>

#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>
#include <components/rtxvulkan/shaders/shared/spritelight.h>

#include "normalspreadpass.hpp"
#include "texturepasses.hpp"

namespace Rtx
{
    TextureArrival::TextureArrival(const Device& device, const VkDeviceSize meansRoom)
        : mSums(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shading sums")
        , mMeansRoom(meansRoom)
        , mMeans(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              "normal spread means")
    {
    }

    VkDeviceSize TextureArrival::meansNeeded(const VkDeviceSize total, const VkDeviceSize largest) const
    {
        return std::min(total, std::max(mMeansRoom, largest));
    }

    void TextureArrival::upload(
        const StagingRun& staged, const Image& image, std::span<const VkBufferImageCopy> regions)
    {
        const auto first = static_cast<std::uint32_t>(mRegions.size());
        for (VkBufferImageCopy region : regions)
        {
            region.bufferOffset += staged.mOffset;
            mRegions.push_back(region);
        }

        mUploads.push_back(Upload{
            .mImage = &image,
            .mStaged = staged,
            .mFirst = first,
            .mCount = static_cast<std::uint32_t>(regions.size()),
        });
    }

    void TextureArrival::clearNeutral(const Image& map)
    {
        mClears.push_back(&map);
    }

    void TextureArrival::shade(const Image& source, const Image& map, const TextureWrap wrap)
    {
        mShades.push_back(Shade{ .mSource = &source, .mMap = &map, .mWrap = wrap });
    }

    void TextureArrival::spread(const Image& map, const Image& spread)
    {
        mSpreads.push_back(Spread{ .mMap = &map, .mSpread = &spread });
    }

    void TextureArrival::bake(const Image& source, const Image& bake)
    {
        mBakes.push_back(Bake{ .mSource = &source, .mBake = &bake });
    }

    void TextureArrival::record(Batch& batch, const TexturePasses& passes, GpuTimer* const timer)
    {
        const bool any
            = !mUploads.empty() || !mClears.empty() || !mShades.empty() || !mSpreads.empty() || !mBakes.empty();
        mRecordedBarriers = 0;
        if (any)
        {
            // Room for the widest barrier of the run, the one after the copies, which holds every
            // image the run writes once.
            const std::size_t widest
                = mUploads.size() + mClears.size() + mShades.size() + mSpreads.size() + mBakes.size();
            mImageBarriers.resize(std::max(widest, mImageBarriers.size()));

            const VkCommandBuffer commands = batch.getCommands();
            Barriers barriers(commands, mImageBarriers);

            // Each phase flushes what the one before left pending before it records, and leaves what
            // it wrote pending as the texture the trace samples: a phase's last barrier and the next
            // phase's first are one command.
            recordWrites(commands, barriers);
            recordShading(commands, barriers, passes, timer);
            recordSpreads(commands, barriers, passes, timer);
            recordBakes(commands, barriers, passes, timer);
            barriers.flush();
            mRecordedBarriers = barriers.getEmitted();
        }

        mRegions.clear();
        mUploads.clear();
        mClears.clear();
        mShades.clear();
        mSpreads.clear();
        mBakes.clear();
    }

    void TextureArrival::recordWrites(const VkCommandBuffer commands, Barriers& barriers)
    {
        // Nothing before them: the head of every command buffer orders it after the last frame.
        for (const Upload& upload : mUploads)
            upload.mImage->addTransition(barriers, Use::sUndefined, Use::sCopyWrite);
        for (const Image* const map : mClears)
            map->addTransition(barriers, Use::sUndefined, Use::sClearWrite);
        barriers.flush();

        for (const Upload& upload : mUploads)
            vkCmdCopyBufferToImage(commands, upload.mStaged.mBuffer, upload.mImage->getHandle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, upload.mCount, &mRegions[upload.mFirst]);

        const VkClearColorValue neutral{ .float32 = { Shaders::shadingUnit(1.0f), 0.0f, 0.0f, 0.0f } };
        for (const Image* const map : mClears)
        {
            const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, map->getMipLevels(), 0, 1 };
            vkCmdClearColorImage(commands, map->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &neutral, 1, &whole);
        }

        // What was written, as the textures they are, and what a dispatch writes next, out of
        // `UNDEFINED`.
        for (const Upload& upload : mUploads)
            upload.mImage->addTransition(barriers, Use::sCopyWrite, Use::sTextureSample);
        for (const Image* const map : mClears)
            map->addTransition(barriers, Use::sClearWrite, Use::sTextureSample);
        for (const Shade& shade : mShades)
            shade.mMap->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        for (const Spread& spread : mSpreads)
            spread.mSpread->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        for (const Bake& bake : mBakes)
            bake.mBake->addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
    }

    void TextureArrival::recordShading(
        const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes, GpuTimer* const timer)
    {
        if (mShades.empty())
            return;
        const GpuZone timed(timer, commands, FrameZone::Shading);

        mSums.outgrow(mShades.size() * ShadingPass::sSumBytes);
        const Buffer& sums = mSums.get();
        const auto sumsOf
            = [&](const std::size_t at) { return sums.describe(at * ShadingPass::sSumBytes, ShadingPass::sSumBytes); };

        // After the copies, because a texture's own chain is what its estimate reads.
        barriers.flush();
        for (std::size_t at = 0; at < mShades.size(); ++at)
            passes.mShading.recordSum(commands, *mShades[at].mSource, sumsOf(at), mShades[at].mWrap);

        barriers.add(memoryBarrier(Use::sBufferComputeWrite, Use::sBufferComputeRead));
        barriers.flush();
        for (std::size_t at = 0; at < mShades.size(); ++at)
        {
            const Shade& shade = mShades[at];
            passes.mShading.recordMap(commands, *shade.mSource, *shade.mMap, sumsOf(at), shade.mWrap);
        }

        for (const Shade& shade : mShades)
            shade.mMap->addTransition(barriers, Use::sComputeWrite, Use::sTextureSample);
    }

    void TextureArrival::recordSpreads(
        const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes, GpuTimer* const timer)
    {
        if (mSpreads.empty())
            return;
        const GpuZone timed(timer, commands, FrameZone::Spread);

        // **In groups whose means fit the room**, each over the room the group before worked in, in
        // the order the spreads came: the largest group is what the means are grown to, and
        // `meansNeeded` what an arrival is priced at, so the two cannot part. A map past the room
        // is a group of its own.
        const auto bytesOf = [](const Spread& spread) {
            return VkDeviceSize{ NormalSpreadPass::meansTexels(*spread.mSpread) } * Shaders::NORMAL_SPREAD_MEAN_BYTES;
        };
        mGroupEnds.clear();
        VkDeviceSize largestGroup = 0;
        VkDeviceSize group = 0;
        for (std::size_t at = 0; at < mSpreads.size(); ++at)
        {
            if (group > 0 && group + bytesOf(mSpreads[at]) > mMeansRoom)
            {
                mGroupEnds.push_back(at);
                group = 0;
            }
            group += bytesOf(mSpreads[at]);
            largestGroup = std::max(largestGroup, group);
        }
        mGroupEnds.push_back(mSpreads.size());
        mMeans.growTo(largestGroup);
        const Buffer& means = mMeans.get();

        // What one level of a group reads of the level before, and what a group's first level
        // writes over what the group before read and wrote.
        const BufferUse readWrite{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };

        std::size_t begin = 0;
        for (const std::size_t end : mGroupEnds)
        {
            std::uint32_t deepest = 0;
            for (std::size_t at = begin; at < end; ++at)
                deepest = std::max(deepest, mSpreads[at].mSpread->getMipLevels());

            for (std::uint32_t level = 0; level < deepest; ++level)
            {
                // Each level reads the means of the one before, and a group's first works in the
                // means the group before used. One memory barrier
                // either way, which the first group's first level needs none of.
                if (level > 0 || begin > 0)
                    barriers.add(memoryBarrier(readWrite, readWrite));
                barriers.flush();

                std::uint32_t meansAt = 0;
                for (std::size_t at = begin; at < end; ++at)
                {
                    const Spread& spread = mSpreads[at];
                    if (level < spread.mSpread->getMipLevels())
                        passes.mSpread.recordLevel(commands, *spread.mMap, *spread.mSpread, level, means, meansAt);
                    meansAt += NormalSpreadPass::meansTexels(*spread.mSpread);
                }
            }
            begin = end;
        }

        for (const Spread& spread : mSpreads)
            spread.mSpread->addTransition(barriers, Use::sComputeWrite, Use::sTextureSample);
    }

    void TextureArrival::recordBakes(
        const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes, GpuTimer* const timer)
    {
        if (mBakes.empty())
            return;
        const GpuZone timed(timer, commands, FrameZone::Bake);

        // After the copies, because a bake's source may have arrived in this run. A stage of every
        // level of every bake at once, and the next after a barrier, since it reads back what this
        // one wrote.
        for (std::uint32_t stage = 0; stage < Shaders::SPRITE_LIGHT_STAGES; ++stage)
        {
            if (stage > 0)
                for (const Bake& bake : mBakes)
                    bake.mBake->addTransition(barriers, Use::sComputeReadWrite, Use::sComputeReadWrite);
            barriers.flush();

            for (const Bake& bake : mBakes)
                for (std::uint32_t level = 0; level < bake.mBake->getMipLevels(); ++level)
                    passes.mBake.recordStage(commands, *bake.mSource, *bake.mBake, level, stage);
        }

        for (const Bake& bake : mBakes)
            bake.mBake->addTransition(barriers, Use::sComputeReadWrite, Use::sTextureSample);
    }
}
