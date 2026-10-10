#include "historyclamppass.hpp"

#include <array>

#include <components/rtxvulkan/device/memory/image.hpp>
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

    void HistoryClampPass::record(
        const VkCommandBuffer commands, const Images& images, const Shaders::HistoryClampConstants& constants) const
    {
        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::HISTORY_CLAMP_BIND_SAMPLED, images.mSampled.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_MEAN, images.mMean.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_FAST, images.mFastBlended.describeStorage());
        writes.image(Shaders::HISTORY_CLAMP_BIND_FAST_OUT, images.mFast.describeStorage());

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(constants.mWidth, constants.mHeight, Shaders::HISTORY_CLAMP_WORKGROUP));
    }
}
