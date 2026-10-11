#include "texturearrival.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <volk.h>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/shaders/shared/normalspread.h>
#include <components/rtxvulkan/shaders/shared/spritelight.h>

#include "bc7encodepass.hpp"
#include "normalspreadpass.hpp"
#include "texturepasses.hpp"

namespace Rtx
{
    TextureArrival::TextureArrival(const Device& device, const VkDeviceSize meansRoom)
        : mSums(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shading sums")
        , mBlocks(device, BufferKind::DeviceLocal,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "encoded chain blocks")
        , mMeansRoom(meansRoom)
        , mMeans(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              "normal spread means")
    {
    }

    VkDeviceSize TextureArrival::meansNeeded(const VkDeviceSize total, const VkDeviceSize largest) const
    {
        return std::min(total, std::max(mMeansRoom, largest));
    }

    void TextureArrival::open(const std::size_t textures)
    {
        assert(mUploads.empty() && mClears.empty() && mChains.empty() && mShades.empty() && mSpreads.empty()
            && mBakes.empty() && mHeld.empty() && "a run opened over one nobody recorded");

        // An upload a chain is made from, and the chain an encode is made from: two a texture at most.
        mHeld.reserve(2 * textures);
    }

    void TextureArrival::upload(
        Batch& batch, const Image& image, std::span<const std::byte> bytes, std::span<const VkBufferImageCopy> regions)
    {
        const StagingRun staged = batch.stage(bytes);
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

    const Image& TextureArrival::hold(Image&& image)
    {
        // A growth would move every image already held out from under the work that names it.
        Crash::contract(mHeld.size() < mHeld.capacity(), "a run held more images than it opened room for");

        return mHeld.emplace_back(std::move(image));
    }

    void TextureArrival::clearNeutral(const Image& map)
    {
        mClears.push_back(&map);
    }

    void TextureArrival::chain(const Image& source, const Image& chain, const bool encoded,
        const TextureEncoding encoding, const Image* const bc7)
    {
        mChains.push_back(
            Chain{ .mSource = &source, .mChain = &chain, .mEncoded = encoded, .mEncoding = encoding, .mBc7 = bc7 });
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

    void TextureArrival::record(Batch& batch, const TexturePasses& passes)
    {
        const bool any = !mUploads.empty() || !mClears.empty() || !mChains.empty() || !mShades.empty()
            || !mSpreads.empty() || !mBakes.empty();
        mRecordedBarriers = 0;
        if (any)
        {
            // Room for the widest barrier of the run, the one after the copies, which holds every
            // image the run writes once.
            const std::size_t widest
                = mUploads.size() + mClears.size() + mChains.size() + mShades.size() + mSpreads.size() + mBakes.size();
            mImageBarriers.resize(std::max(widest, mImageBarriers.size()));

            const VkCommandBuffer commands = batch.getCommands();
            Barriers barriers(commands, mImageBarriers);

            // Each phase flushes what the one before left pending before it records, and leaves what
            // it wrote pending as the texture the trace samples: a phase's last barrier and the next
            // phase's first are one command.
            recordWrites(commands, barriers);
            recordChains(commands, barriers, passes);
            recordEncodes(commands, barriers, passes);
            recordShading(commands, barriers, passes);
            recordSpreads(commands, barriers, passes);
            recordBakes(commands, barriers, passes);
            barriers.flush();
            mRecordedBarriers = barriers.getEmitted();
        }

        for (Image& held : mHeld)
            batch.keep(std::move(held));

        mRegions.clear();
        mUploads.clear();
        mClears.clear();
        mChains.clear();
        mShades.clear();
        mSpreads.clear();
        mBakes.clear();
        mHeld.clear();
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
        // `UNDEFINED`: read and written from a chain's first level, for the reason
        // `MipChainPass::recordLevel` gives.
        for (const Upload& upload : mUploads)
            upload.mImage->addTransition(barriers, Use::sCopyWrite, Use::sTextureSample);
        for (const Image* const map : mClears)
            map->addTransition(barriers, Use::sClearWrite, Use::sTextureSample);
        for (const Chain& chain : mChains)
            chain.mChain->addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
        for (const Shade& shade : mShades)
            shade.mMap->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        for (const Spread& spread : mSpreads)
            spread.mSpread->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        for (const Bake& bake : mBakes)
            bake.mBake->addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
    }

    void TextureArrival::recordChains(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        std::uint32_t deepest = 0;
        for (const Chain& chain : mChains)
            deepest = std::max(deepest, chain.mChain->getMipLevels());

        for (std::uint32_t level = 0; level < deepest; ++level)
        {
            // Each level reads the one before it, of every chain at once: one memory barrier, for
            // a chain stays in one layout from its first level to its last.
            if (level > 0)
                for (const Chain& chain : mChains)
                    if (level < chain.mChain->getMipLevels())
                        chain.mChain->addTransition(barriers, Use::sComputeReadWrite, Use::sComputeReadWrite);
            barriers.flush();

            for (const Chain& chain : mChains)
                if (level < chain.mChain->getMipLevels())
                    passes.mChain.recordLevel(
                        commands, *chain.mSource, *chain.mChain, level, chain.mEncoded, chain.mEncoding);
        }

        // A chain kept as BC7 is read by the encode next, as storage, and never sampled.
        for (const Chain& chain : mChains)
            chain.mChain->addTransition(
                barriers, Use::sComputeReadWrite, chain.mBc7 != nullptr ? Use::sComputeRead : Use::sTextureSample);
    }

    void TextureArrival::recordEncodes(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        VkDeviceSize largest = 0;
        for (const Chain& chain : mChains)
            if (chain.mBc7 != nullptr)
                largest = std::max(largest,
                    Bc7Chain::of(chain.mChain->getWidth(), chain.mChain->getHeight(), chain.mChain->getMipLevels())
                        .mBytes);
        if (largest == 0)
            return;

        mBlocks.outgrow(largest);
        barriers.flush();

        // One encode after another through one room, each handing it to the next once its copy has
        // read it, as the ground's composite does: only a file past `sLargestLooseChainSide` with no
        // levels of its own comes here, which a run holds few of.
        for (const Chain& chain : mChains)
        {
            if (chain.mBc7 == nullptr)
                continue;

            passes.mEncode.record(commands, *chain.mChain, mBlocks.get(), *chain.mBc7, true);
            handOver(commands, Use::sBufferCopyRead, Use::sBufferComputeWrite);
        }
    }

    void TextureArrival::recordShading(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        if (mShades.empty())
            return;

        mSums.outgrow(mShades.size() * ShadingPass::sSumBytes);
        const Buffer& sums = mSums.get();
        const auto sumsOf
            = [&](const std::size_t at) { return sums.describe(at * ShadingPass::sSumBytes, ShadingPass::sSumBytes); };

        // After the chains, because a texture's own chain is what its estimate reads.
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

    void TextureArrival::recordSpreads(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        if (mSpreads.empty())
            return;

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
                // Each level reads the means of the one before, as a chain reads its level above,
                // and a group's first works in the means the group before used. One memory barrier
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

    void TextureArrival::recordBakes(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        if (mBakes.empty())
            return;

        // After the chains, because a bake's source may have arrived in this run. A stage of every
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
