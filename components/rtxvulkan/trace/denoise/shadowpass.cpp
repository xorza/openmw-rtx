#include "shadowpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_MASK_BINDINGS> sMaskBindings
            = computeBindings<Shaders::SHADOW_MASK_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_TILES_BINDINGS> sTilesBindings
            = computeBindings<Shaders::SHADOW_TILES_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SHADOW_FILTER_BINDINGS> sFilterBindings
            = computeBindings<Shaders::SHADOW_FILTER_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        ComputePipeline makeFilter(
            const Device& device, const std::filesystem::path& shaderDirectory, const std::uint32_t level)
        {
            const std::array<std::uint32_t, 1> specialization{ level };
            return ComputePipeline(device, sFilterBindings, sizeof(Shaders::ShadowFilterConstants), {},
                shaderDirectory / "shadowfilter.comp.spv", "shadow-filter", specialization);
        }

        /// Orders one dispatch against the next over every image either may touch. **One barrier
        /// over all of them, and each both ways**: the temporal pass reads the history the first
        /// level then writes, and the levels take the scratch and the history by turns, so a
        /// dependency that named only what the last dispatch wrote would leave a write after a read
        /// unordered.
        void orderDispatches(VkCommandBuffer commands, std::span<const Image* const> images)
        {
            Barriers between(commands);
            for (const Image* image : images)
                between.add(image->describeTransition(Use::sComputeReadWrite, Use::sComputeReadWrite));
            between.flush();
        }
    }

    ShadowPass::ShadowPass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mMask(device, sMaskBindings, sizeof(Shaders::ShadowMaskConstants), {},
            shaderDirectory / "shadowmask.comp.spv", "shadow-mask")
        , mTiles(device, sTilesBindings, sizeof(Shaders::ShadowTilesConstants), {},
              shaderDirectory / "shadowtiles.comp.spv", "shadow-tiles")
        , mFilters{ makeFilter(device, shaderDirectory, 0), makeFilter(device, shaderDirectory, 1),
            makeFilter(device, shaderDirectory, 2) }
    {
    }

    const Image& ShadowPass::record(
        VkCommandBuffer commands, const ShadowHistory::Turn& turn, const GBuffer& buffer, const Frame& frame) const
    {
        const std::uint32_t width = frame.mCamera.mWidth;
        const std::uint32_t height = frame.mCamera.mHeight;
        assert(turn.mVisibility.getWidth() >= width && turn.mVisibility.getHeight() >= height);
        assert(buffer.getWidth() >= width && buffer.getHeight() >= height);

        // What this frame writes whole is discarded, and the history and the moments the temporal
        // pass reads are discarded too where there are none — as the accumulator's are, so the reset
        // is a statement about the history and not about the memory. The last frame's accesses are
        // behind the head barrier `CommandPool::begin` recorded.
        {
            Barriers barriers(commands);
            if (turn.mFresh)
                for (const Image* image : { &turn.mHistory, &turn.mMomentsBefore })
                    barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeRead));

            for (const Image* image : { &turn.mScratch, &turn.mMoments, &turn.mVisibility, &turn.mTiles, &turn.mMask })
                barriers.add(image->describeTransition(Use::sUndefined, Use::sComputeWrite));

            barriers.flush();
        }

        const std::array<const Image*, 4> taken{ &turn.mHistory, &turn.mScratch, &turn.mTiles, &turn.mVisibility };

        {
            DescriptorWrites<Shaders::SHADOW_MASK_BINDINGS> writes;
            writes.image(Shaders::SHADOW_MASK_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
            writes.image(Shaders::SHADOW_MASK_BIND_MASK, turn.mMask.describeStorage());

            dispatch(commands, mMask, writes.get(), Shaders::ShadowMaskConstants{ .mWidth = width, .mHeight = height },
                groupsFor(width, Shaders::SHADOW_MASK_WIDTH), groupsFor(height, 2 * Shaders::SHADOW_MASK_HEIGHT));
        }

        turn.mMask.transition(commands, Use::sComputeWrite, Use::sComputeRead);

        {
            DescriptorWrites<Shaders::SHADOW_TILES_BINDINGS> writes;
            writes.image(Shaders::SHADOW_TILES_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_HELD_SURFACE, frame.mHeld.mImage.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_HISTORY, turn.mHistory.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS_BEFORE, turn.mMomentsBefore.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS, turn.mMoments.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_REPROJECTED, turn.mScratch.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_TILES, turn.mTiles.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MASK, turn.mMask.describeStorage());
            assert(
                writes.size() == Shaders::SHADOW_TILES_BINDINGS && "a binding the layout declares was left unwritten");

            const Shaders::ShadowTilesConstants constants{
                .mCamera = frame.mCamera,
                .mReset = (frame.mReset || frame.mHeld.mFresh || turn.mFresh) ? 1u : 0u,
                .mDistanceScale = frame.mHeld.mDistanceScale,
            };

            dispatch(commands, mTiles, writes.get(), constants, groupsFor(width, Shaders::SHADOW_WORKGROUP),
                groupsFor(height, Shaders::SHADOW_WORKGROUP));
        }

        // The SDK's order: the temporal blend into the first level, whose answer is the history the
        // next frame reads; the second back into the scratch, which a cleared tile leaves holding the
        // temporal pass's exact value; and the third into what the composite reads.
        const std::array<const Image*, Shaders::SHADOW_FILTER_LEVELS> sources{ &turn.mScratch, &turn.mHistory,
            &turn.mScratch };
        const std::array<const Image*, Shaders::SHADOW_FILTER_LEVELS> targets{ &turn.mHistory, &turn.mScratch,
            &turn.mVisibility };

        const Shaders::ShadowFilterConstants constants{ .mCamera = frame.mCamera, .mArms = frame.mArms };
        for (std::uint32_t level = 0; level < Shaders::SHADOW_FILTER_LEVELS; ++level)
        {
            orderDispatches(commands, taken);

            DescriptorWrites<Shaders::SHADOW_FILTER_BINDINGS> writes;
            writes.image(Shaders::SHADOW_FILTER_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_PUFFS, buffer.get(Channel::Puffs).describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_TILES, turn.mTiles.describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_SOURCE, sources[level]->describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_FILTERED, targets[level]->describeStorage());

            dispatch(commands, mFilters[level], writes.get(), constants, groupsFor(width, Shaders::SHADOW_WORKGROUP),
                groupsFor(height, Shaders::SHADOW_WORKGROUP));
        }

        turn.mVisibility.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return turn.mVisibility;
    }
}
