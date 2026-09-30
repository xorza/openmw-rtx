#include "compositepass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/composite.h>
#include <components/rtx/shaders/shadow.h>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        /// Seven channels in, the direct one written back as the frame, the shadow denoiser's answer
        /// and the running sum — all storage images, all pushed.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::COMPOSITE_BINDINGS> sBindings
            = computeBindings<Shaders::COMPOSITE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    CompositePass::CompositePass(const Device& device)
        : mPipeline(device, sBindings, {}, "composite.comp.spv", "composite")
        , mNoSum(makeStandIn(device, toVulkanFormat(COMPOSITE_SUM_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, "no-sum"))
        , mNoShadow(makeStandIn(device, toVulkanFormat(SHADOW_REPROJECTED), VK_IMAGE_USAGE_STORAGE_BIT, "no-shadow"))
    {
    }

    void CompositePass::record(VkCommandBuffer commands, const GBuffer& buffer, const Denoised& denoised,
        const Image* sum, Shaders::CompositeConstants constants) const
    {
        assert(constants.mShadowed == 0 && "the shadow is the denoiser's to say, and not the caller's");
        constants.mShadowed = denoised.mShadow != nullptr ? 1u : 0u;

        const Image& indirect = denoised.mIndirect;
        const Image& specular = denoised.mSpecular;
        const Image& pane = denoised.mPane;
        assert(buffer.getWidth() >= constants.mWidth && buffer.getHeight() >= constants.mHeight);
        assert(indirect.getWidth() >= constants.mWidth && indirect.getHeight() >= constants.mHeight);
        assert(specular.getWidth() >= constants.mWidth && specular.getHeight() >= constants.mHeight);
        assert(pane.getWidth() >= constants.mWidth && pane.getHeight() >= constants.mHeight);

        // A sum has to cover the frame it is a sum of; a stand-in never read does not.
        assert(constants.mAccumulate == 0 || sum != nullptr);
        assert(sum == nullptr || (sum->getWidth() >= constants.mWidth && sum->getHeight() >= constants.mHeight));

        // The real sum is the caller's to order; the stand-in is touched by one composite a
        // command buffer, and the head barrier `CommandPool::begin` recorded orders that after the
        // last one.
        const Image& bound = sum != nullptr ? *sum : mNoSum;
        const Image& shadowBound = denoised.mShadow != nullptr ? *denoised.mShadow : mNoShadow;

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::COMPOSITE_BIND_DIRECT, buffer.get(Channel::Direct).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_INDIRECT, indirect.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_ALBEDO, buffer.get(Channel::Albedo).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SUM, bound.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SHADOW, shadowBound.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SPECULAR, specular.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_PANE, pane.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_PANE_ALBEDO, buffer.get(Channel::PaneAlbedo).describeStorage());

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(constants.mWidth, constants.mHeight, Shaders::COMPOSITE_WORKGROUP));
    }
}
