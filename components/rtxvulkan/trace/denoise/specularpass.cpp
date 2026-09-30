#include "specularpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/shaders/specular.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SPECULAR_BINDINGS> sBindings
            = computeBindings<Shaders::SPECULAR_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    SpecularPass::SpecularPass(const Device& device)
        : mPipeline(device, sBindings, {}, "specular.comp.spv", "specular")
    {
    }

    const Image& SpecularPass::record(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::VisibilityConstants& sampled = frame.mSampled;
        const std::uint32_t width = sampled.mCamera.mWidth;
        const std::uint32_t height = sampled.mCamera.mHeight;
        assert(images.mMean.getWidth() >= width && images.mMean.getHeight() >= height);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::SPECULAR_BIND_SPECULAR, buffer.get(Channel::Specular).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_PUFFS, buffer.get(Channel::Puffs).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_HELD_SURFACE, images.mHeldSurface.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN_BEFORE, images.mMeanBefore.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN, images.mMean.describeStorage());

        const Shaders::SpecularConstants constants{
            .mCamera = sampled.mCamera,
            .mArms = sampled.mArms,
            .mPreviousForward = sampled.mPreviousForward,
            .mPreviousRight = sampled.mPreviousRight,
            .mPreviousUp = sampled.mPreviousUp,
            .mArmsSpread = sampled.mArmsSpread,
            .mReset = images.mFresh ? 1u : 0u,
            .mDistanceScale = frame.mDistanceScale,
        };

        dispatch(commands, mPipeline, writes, constants, Groups::covering(width, height, Shaders::SPECULAR_WORKGROUP));

        images.mMean.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return images.mMean;
    }
}
