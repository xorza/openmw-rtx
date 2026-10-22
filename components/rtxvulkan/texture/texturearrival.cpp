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
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/graveyard.hpp>
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
    TextureArrival::TextureArrival(const Device& device, const VkDeviceSize meansRoom, const VkDeviceSize looseRoom)
        : mSums(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shading sums")
        , mBlocks(device, BufferKind::DeviceLocal,
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, "encoded chain blocks")
        , mMeansRoom(meansRoom)
        , mMeans(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
              "normal spread means")
        , mDevice(device)
        , mLooseRoom(looseRoom)
    {
    }

    TextureArrival::~TextureArrival()
    {
        if (mCanvas.getHandle() != VK_NULL_HANDLE)
            mDevice.getGraveyard().bury(nullptr, 0, std::move(mCanvas));
    }

    VkDeviceSize TextureArrival::meansNeeded(const VkDeviceSize total, const VkDeviceSize largest) const
    {
        return std::min(total, std::max(mMeansRoom, largest));
    }

    VkDeviceSize TextureArrival::looseNeeded(const VkDeviceSize total, const VkDeviceSize largest) const
    {
        return std::min(total, std::max(mLooseRoom, largest));
    }

    void TextureArrival::open(const std::size_t textures)
    {
        assert(mUploads.empty() && mClears.empty() && mChains.empty() && mEncodes.empty() && mShades.empty()
            && mSpreads.empty() && mBakes.empty() && mHeld.empty() && "a run opened over one nobody recorded");

        // An upload a chain is made from, and the loose chain an encode is made from: two a texture
        // at most.
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

    void TextureArrival::chain(
        const Image& source, const Image& chain, const bool encoded, const TextureEncoding encoding)
    {
        mChains.push_back(Chain{ .mSource = &source, .mChain = &chain, .mEncoded = encoded, .mEncoding = encoding });
    }

    void TextureArrival::encode(const Image& source, const ImageDescription& loose, const bool encoded,
        const TextureEncoding encoding, const Image& bc7)
    {
        mEncodes.push_back(
            Encode{ .mSource = &source, .mLoose = loose, .mEncoded = encoded, .mEncoding = encoding, .mBc7 = &bc7 });
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
        const bool any = !mUploads.empty() || !mClears.empty() || !mChains.empty() || !mEncodes.empty()
            || !mShades.empty() || !mSpreads.empty() || !mBakes.empty();
        mRecordedBarriers = 0;
        if (any)
        {
            // Room for the widest barrier of the run, the one after the copies, which holds every
            // image the run writes once.
            const std::size_t widest = mUploads.size() + mClears.size() + mChains.size() + mEncodes.size()
                + mShades.size() + mSpreads.size() + mBakes.size();
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
        mEncodes.clear();
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

        for (const Chain& chain : mChains)
            chain.mChain->addTransition(barriers, Use::sComputeReadWrite, Use::sTextureSample);
    }

    void TextureArrival::recordEncodes(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        if (mEncodes.empty())
            return;

        // **In groups whose loose chains fit the room**, each made over the canvas the group before
        // worked in, in the order the files came, side by side inside a group on each chain's own
        // alignment. A chain past the room is a group of its own. The canvas is grown to the largest
        // group and kept, and what an arrival is priced at is `looseNeeded`.
        mLooseAt.clear();
        mLooseGroupEnds.clear();
        VkDeviceSize group = 0;
        VkDeviceSize largestGroup = 0;
        VkDeviceSize alignment = 1;
        std::uint32_t types = ~0u;
        VkDeviceSize largestBlocks = 0;
        for (std::size_t at = 0; at < mEncodes.size(); ++at)
        {
            const VkMemoryRequirements asked = Image::requirementsFor(mDevice, mEncodes[at].mLoose);
            alignment = std::max(alignment, asked.alignment);
            types &= asked.memoryTypeBits;

            VkDeviceSize offset = alignUp(group, asked.alignment);
            if (group > 0 && offset + asked.size > mLooseRoom)
            {
                mLooseGroupEnds.push_back(at);
                offset = 0;
            }
            mLooseAt.push_back(offset);
            group = offset + asked.size;
            largestGroup = std::max(largestGroup, group);

            const ImageDescription& loose = mEncodes[at].mLoose;
            largestBlocks = std::max(largestBlocks, Bc7Chain::of(loose.mWidth, loose.mHeight, loose.mMipLevels).mBytes);
        }
        mLooseGroupEnds.push_back(mEncodes.size());

        // Made again only where the groups outgrew it or it cannot hold these chains: the old buried
        // under the next submit, after every chain made in it, which went with their batches.
        if (largestGroup > mCanvasBytes || alignment > mCanvasAlignment || (types & mCanvasTypes) != mCanvasTypes)
        {
            if (mCanvas.getHandle() != VK_NULL_HANDLE)
                mDevice.getGraveyard().bury(nullptr, 0, std::move(mCanvas));
            mCanvasBytes = std::max(largestGroup, mCanvasBytes);
            mCanvasAlignment = std::max(alignment, mCanvasAlignment);
            mCanvasTypes = types;
            mCanvas = mDevice.getMemory().take(
                VkMemoryRequirements{ .size = mCanvasBytes, .alignment = mCanvasAlignment, .memoryTypeBits = types },
                MemoryUse::Essential);
        }

        // To the largest chain and no further, as the means are: `TextureArray::costAt` charges an
        // arrival what this asks past what the blocks hold, and a doubling would take more.
        mBlocks.growTo(largestBlocks);

        std::size_t begin = 0;
        for (const std::size_t end : mLooseGroupEnds)
        {
            // Every loose chain of the group, made in the canvas and laid out from what any chain
            // before it wrote there and its encodes read: the group before, or a run recorded into
            // the same batch before this one.
            const std::size_t first = mHeld.size();
            for (std::size_t at = begin; at < end; ++at)
                hold(Image(mDevice, mEncodes[at].mLoose, mCanvas, mLooseAt[at], "loose chain"));
            for (std::size_t at = begin; at < end; ++at)
                mHeld[first + at - begin].addTransition(barriers, Use::sAliased, Use::sComputeReadWrite);

            std::uint32_t deepest = 0;
            for (std::size_t at = begin; at < end; ++at)
                deepest = std::max(deepest, mEncodes[at].mLoose.mMipLevels);

            for (std::uint32_t level = 0; level < deepest; ++level)
            {
                if (level > 0)
                    for (std::size_t at = begin; at < end; ++at)
                        if (level < mEncodes[at].mLoose.mMipLevels)
                            mHeld[first + at - begin].addTransition(
                                barriers, Use::sComputeReadWrite, Use::sComputeReadWrite);
                barriers.flush();

                for (std::size_t at = begin; at < end; ++at)
                {
                    const Encode& encode = mEncodes[at];
                    if (level < encode.mLoose.mMipLevels)
                        passes.mChain.recordLevel(commands, *encode.mSource, mHeld[first + at - begin], level,
                            encode.mEncoded, encode.mEncoding);
                }
            }

            // Read by the encode, as storage, and never sampled. One encode after another through
            // one room of blocks, each handing it to the next once its copy has read it, as the
            // ground's composite does.
            for (std::size_t at = begin; at < end; ++at)
                mHeld[first + at - begin].addTransition(barriers, Use::sComputeReadWrite, Use::sComputeRead);
            barriers.flush();
            for (std::size_t at = begin; at < end; ++at)
            {
                passes.mEncode.record(commands, mHeld[first + at - begin], mBlocks.get(), *mEncodes[at].mBc7, true);
                handOver(commands, Use::sBufferCopyRead, Use::sBufferComputeWrite);
            }
            begin = end;
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
