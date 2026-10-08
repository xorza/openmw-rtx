#include "historyclamppass.hpp"

#include <array>

#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/pane.h>
#include <components/rtxvulkan/shaders/shared/specular.h>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::HISTORY_CLAMP_BINDINGS> sBindings
            = computeBindings<Shaders::HISTORY_CLAMP_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        static_assert(SPECULAR_MEAN == HISTORY_CLAMP_MEAN && PANE_MEAN == HISTORY_CLAMP_MEAN,
            "a filter's slow mean in another format than the clamp holds it in");
    }

    HistoryClampPass::HistoryClampPass(const Device& device)
        : mPipeline(device, sBindings, {}, "historyclamp.comp.spv", "history clamp")
    {
    }

    void HistoryClampPass::record(const VkCommandBuffer commands, const Images& images, const std::uint32_t width,
        const std::uint32_t height, const bool antilag) const
    {
        // The clamp reads a neighbour's fast blend, so every pixel's is behind it, holds the slow
        // mean the filter wrote in place, and writes the fast mean the filter just read as last
        // frame's.
        Barriers written(commands);
        images.mMean.addTransition(written, Use::sComputeWrite, Use::sComputeReadWrite);
        images.mFastBlended.addTransition(written, Use::sComputeWrite, Use::sComputeRead);
        images.mFast.addTransition(written, Use::sComputeRead, Use::sComputeWrite);
        written.flush();

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::HISTORY_CLAMP_BIND_SAMPLED, images.mSampled.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_MEAN, images.mMean.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_FAST, images.mFastBlended.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_FAST_OUT, images.mFast.describeStorage());

        dispatch(commands, mPipeline, writes,
            Shaders::HistoryClampConstants{ .mWidth = width, .mHeight = height, .mAntilag = antilag ? 1u : 0u },
            Groups::covering(width, height, Shaders::HISTORY_CLAMP_WORKGROUP));
    }
}
