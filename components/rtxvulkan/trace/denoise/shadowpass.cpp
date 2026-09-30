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

        ComputePipeline<Shaders::ShadowFilterConstants> makeFilter(const Device& device, const std::uint32_t level)
        {
            std::array<std::uint32_t, Shaders::SHADOW_SPEC_COUNT> specialization{};
            specialization[Shaders::SHADOW_SPEC_LEVEL] = level;
            return ComputePipeline<Shaders::ShadowFilterConstants>(
                device, sFilterBindings, {}, "shadowfilter.comp.spv", "shadow-filter", specialization);
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

    ShadowPass::ShadowPass(const Device& device)
        : mMask(device, sMaskBindings, {}, "shadowmask.comp.spv", "shadow-mask")
        , mTiles(device, sTilesBindings, {}, "shadowtiles.comp.spv", "shadow-tiles")
        , mFilters{ makeFilter(device, 0), makeFilter(device, 1), makeFilter(device, 2) }
    {
    }

    const Image& ShadowPass::record(VkCommandBuffer commands, const DenoiseHistory::ShadowImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mCamera;
        const std::uint32_t width = camera.mWidth;
        const std::uint32_t height = camera.mHeight;
        assert(images.mVisibility.getWidth() >= width && images.mVisibility.getHeight() >= height);
        assert(buffer.getWidth() >= width && buffer.getHeight() >= height);

        const std::array<const Image*, 4> taken{ &images.mHistory, &images.mScratch, &images.mTiles,
            &images.mVisibility };

        {
            DescriptorWrites writes(mMask);
            writes.image(Shaders::SHADOW_MASK_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
            writes.image(Shaders::SHADOW_MASK_BIND_MASK, images.mMask.describeStorage());

            dispatch(commands, mMask, writes, Shaders::ShadowMaskConstants{ .mWidth = width, .mHeight = height },
                Groups::covering(width, height, Shaders::SHADOW_MASK_WIDTH, 2 * Shaders::SHADOW_MASK_HEIGHT));
        }

        images.mMask.transition(commands, Use::sComputeWrite, Use::sComputeRead);

        {
            DescriptorWrites writes(mTiles);
            writes.image(Shaders::SHADOW_TILES_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_HELD_SURFACE, images.mHeldSurface.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_HISTORY, images.mHistory.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS_BEFORE, images.mMomentsBefore.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MOMENTS, images.mMoments.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_REPROJECTED, images.mScratch.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_TILES, images.mTiles.describeStorage());
            writes.image(Shaders::SHADOW_TILES_BIND_MASK, images.mMask.describeStorage());

            const Shaders::HistoryConstants constants{
                .mCamera = camera,
                .mReset = images.mFresh ? 1u : 0u,
                .mDistanceScale = frame.mDistanceScale,
            };

            dispatch(commands, mTiles, writes, constants, Groups::covering(width, height, Shaders::SHADOW_WORKGROUP));
        }

        // The SDK's order: the temporal blend into the first level, whose answer is the history the
        // next frame reads; the second back into the scratch, which a cleared tile leaves holding the
        // temporal pass's exact value; and the third into what the composite reads.
        const std::array<const Image*, Shaders::SHADOW_FILTER_LEVELS> sources{ &images.mScratch, &images.mHistory,
            &images.mScratch };
        const std::array<const Image*, Shaders::SHADOW_FILTER_LEVELS> targets{ &images.mHistory, &images.mScratch,
            &images.mVisibility };

        const Shaders::ShadowFilterConstants constants{ .mCamera = camera, .mArms = frame.mSampled.mArms };
        for (std::uint32_t level = 0; level < Shaders::SHADOW_FILTER_LEVELS; ++level)
        {
            orderDispatches(commands, taken);

            DescriptorWrites writes(mFilters[level]);
            writes.image(Shaders::SHADOW_FILTER_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_TILES, images.mTiles.describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_SOURCE, sources[level]->describeStorage());
            writes.image(Shaders::SHADOW_FILTER_BIND_FILTERED, targets[level]->describeStorage());

            dispatch(commands, mFilters[level], writes, constants,
                Groups::covering(width, height, Shaders::SHADOW_WORKGROUP));
        }

        images.mVisibility.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return images.mVisibility;
    }
}
