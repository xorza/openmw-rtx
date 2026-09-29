#include "compositepass.hpp"

#include <array>
#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
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
        /// Four channels in, the direct one written back as the frame, the shadow denoiser's answer
        /// and the running sum — all storage images, all pushed.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::COMPOSITE_BINDINGS> sBindings
            = computeBindings<Shaders::COMPOSITE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    CompositePass::CompositePass(const Device& device, const std::filesystem::path& shaderDirectory)
        : mPipeline(device, sBindings, sizeof(Shaders::CompositeConstants), {}, shaderDirectory / "composite.comp.spv",
            "composite")
        , mNoSum(makeStandIn(device, toVulkanFormat(COMPOSITE_SUM_FORMAT), VK_IMAGE_USAGE_STORAGE_BIT, "no-sum"))
        , mNoShadow(makeStandIn(device, toVulkanFormat(SHADOW_REPROJECTED), VK_IMAGE_USAGE_STORAGE_BIT, "no-shadow"))
    {
    }

    void CompositePass::record(VkCommandBuffer commands, const GBuffer& buffer, const Image& indirect,
        const Image* shadow, const Image* sum, const Shaders::CompositeConstants& constants) const
    {
        assert((constants.mShadowed != 0) == (shadow != nullptr)
            && "a shadow handed over and not read, or read and not handed over");
        assert(buffer.getWidth() >= constants.mWidth && buffer.getHeight() >= constants.mHeight);
        assert(indirect.getWidth() >= constants.mWidth && indirect.getHeight() >= constants.mHeight);

        // A sum has to cover the frame it is a sum of; a stand-in never read does not.
        assert(constants.mAccumulate == 0 || sum != nullptr);
        assert(sum == nullptr || (sum->getWidth() >= constants.mWidth && sum->getHeight() >= constants.mHeight));

        // The real sum is the caller's to order; the stand-in is touched by one composite a
        // command buffer, and the head barrier `CommandPool::begin` recorded orders that after the
        // last one.
        const Image& bound = sum != nullptr ? *sum : mNoSum;
        const Image& shadowBound = shadow != nullptr ? *shadow : mNoShadow;

        DescriptorWrites<Shaders::COMPOSITE_BINDINGS> writes;
        writes.image(Shaders::COMPOSITE_BIND_DIRECT, buffer.get(Channel::Direct).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_INDIRECT, indirect.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_ALBEDO, buffer.get(Channel::Albedo).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SUM, bound.describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SUNLIT, buffer.get(Channel::Sunlit).describeStorage());
        writes.image(Shaders::COMPOSITE_BIND_SHADOW, shadowBound.describeStorage());

        dispatch(commands, mPipeline, writes.get(), constants,
            groupsFor(constants.mWidth, Shaders::COMPOSITE_WORKGROUP),
            groupsFor(constants.mHeight, Shaders::COMPOSITE_WORKGROUP));
    }
}
