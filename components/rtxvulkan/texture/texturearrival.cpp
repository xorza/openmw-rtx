#include "texturearrival.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/shaders/shadingmap.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>

#include "texturepasses.hpp"

namespace Rtx
{
    TextureArrival::TextureArrival(const Device& device)
        : mSums(device, BufferKind::DeviceLocal, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, "shading sums")
    {
    }

    void TextureArrival::open(const std::size_t textures)
    {
        assert(mUploads.empty() && mClears.empty() && mChains.empty() && mShades.empty() && mSpreads.empty()
            && mBakes.empty() && mHeld.empty() && "a run opened over one nobody recorded");

        // An upload a chain is made from, and the means of a spread: two a texture at most.
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

    void TextureArrival::chain(const Image& source, const Image& chain, const bool encoded)
    {
        mChains.push_back(Chain{ .mSource = &source, .mChain = &chain, .mEncoded = encoded });
    }

    void TextureArrival::shade(const Image& source, const Image& map, const bool punchThrough)
    {
        mShades.push_back(Shade{ .mSource = &source, .mMap = &map, .mPunchThrough = punchThrough });
    }

    void TextureArrival::spread(const Image& map, const Image& means, const Image& spread)
    {
        mSpreads.push_back(Spread{ .mMap = &map, .mMeans = &means, .mSpread = &spread });
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
            // image the run writes once: a spread's means beside its spread.
            const std::size_t widest = mUploads.size() + mClears.size() + mChains.size() + mShades.size()
                + 2 * mSpreads.size() + mBakes.size();
            mImageBarriers.resize(std::max(widest, mImageBarriers.size()));

            const VkCommandBuffer commands = batch.getCommands();
            Barriers barriers(commands, mImageBarriers);

            // Each phase flushes what the one before left pending before it records, and leaves what
            // it wrote pending as the texture the trace samples: a phase's last barrier and the next
            // phase's first are one command.
            recordWrites(commands, barriers);
            recordChains(commands, barriers, passes);
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
        // `MipChainPass::recordLevel` gives, and a spread's means the same.
        for (const Upload& upload : mUploads)
            upload.mImage->addTransition(barriers, Use::sCopyWrite, Use::sTextureSample);
        for (const Image* const map : mClears)
            map->addTransition(barriers, Use::sClearWrite, Use::sTextureSample);
        for (const Chain& chain : mChains)
            chain.mChain->addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
        for (const Shade& shade : mShades)
            shade.mMap->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        for (const Spread& spread : mSpreads)
        {
            spread.mMeans->addTransition(barriers, Use::sUndefined, Use::sComputeReadWrite);
            spread.mSpread->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
        }
        for (const Bake& bake : mBakes)
            bake.mBake->addTransition(barriers, Use::sUndefined, Use::sComputeWrite);
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
                    passes.mChain.recordLevel(commands, *chain.mSource, *chain.mChain, level, chain.mEncoded);
        }

        for (const Chain& chain : mChains)
            chain.mChain->addTransition(barriers, Use::sComputeReadWrite, Use::sTextureSample);
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
            passes.mShading.recordSum(commands, *mShades[at].mSource, sumsOf(at), mShades[at].mPunchThrough);

        barriers.add(memoryBarrier(Use::sBufferComputeWrite, Use::sBufferComputeRead));
        barriers.flush();
        for (std::size_t at = 0; at < mShades.size(); ++at)
        {
            const Shade& shade = mShades[at];
            passes.mShading.recordMap(commands, *shade.mSource, *shade.mMap, sumsOf(at), shade.mPunchThrough);
        }

        for (const Shade& shade : mShades)
            shade.mMap->addTransition(barriers, Use::sComputeWrite, Use::sTextureSample);
    }

    void TextureArrival::recordSpreads(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        std::uint32_t deepest = 0;
        for (const Spread& spread : mSpreads)
            deepest = std::max(deepest, spread.mSpread->getMipLevels());

        for (std::uint32_t level = 0; level < deepest; ++level)
        {
            // Each level reads the means of the one before, as a chain reads its level above.
            if (level > 0)
                for (const Spread& spread : mSpreads)
                    if (level < spread.mSpread->getMipLevels())
                        spread.mMeans->addTransition(barriers, Use::sComputeReadWrite, Use::sComputeReadWrite);
            barriers.flush();

            for (const Spread& spread : mSpreads)
                if (level < spread.mSpread->getMipLevels())
                    passes.mSpread.recordLevel(commands, *spread.mMap, *spread.mMeans, *spread.mSpread, level);
        }

        for (const Spread& spread : mSpreads)
            spread.mSpread->addTransition(barriers, Use::sComputeWrite, Use::sTextureSample);
    }

    void TextureArrival::recordBakes(const VkCommandBuffer commands, Barriers& barriers, const TexturePasses& passes)
    {
        if (mBakes.empty())
            return;

        // After the chains, because a bake's source may have arrived in this run.
        barriers.flush();
        for (const Bake& bake : mBakes)
            for (std::uint32_t level = 0; level < bake.mBake->getMipLevels(); ++level)
                passes.mBake.recordLevel(commands, *bake.mSource, *bake.mBake, level);

        for (const Bake& bake : mBakes)
            bake.mBake->addTransition(barriers, Use::sComputeWrite, Use::sTextureSample);
    }
}
