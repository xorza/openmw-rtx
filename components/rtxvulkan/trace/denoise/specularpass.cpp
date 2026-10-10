#include "specularpass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/specular.h>
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

    void SpecularPass::record(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::VisibilityConstants& sampled = frame.mSampled;
        const std::uint32_t width = sampled.mEyes.mWorld.mWidth;
        const std::uint32_t height = sampled.mEyes.mWorld.mHeight;
        assert(images.mMean.getWidth() >= width && images.mMean.getHeight() >= height);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::SPECULAR_BIND_SPECULAR, buffer.get(Channel::Specular).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_HELD_SURFACE, buffer.getHeld(Channel::Surface).describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN_BEFORE, images.mMeanBefore.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_MEAN, images.mMean.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FAST_BEFORE, images.mFast.describeStorage());
        writes.image(Shaders::SPECULAR_BIND_FAST_BLENDED, images.mFastBlended.describeStorage());

        dispatch(commands, mPipeline, writes, frame.history(images.mFresh),
            Groups::covering(width, height, Shaders::SPECULAR_WORKGROUP));
    }

    const Image& SpecularPass::recordClamp(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame, const HistoryClampPass& clamp) const
    {
        const std::uint32_t width = frame.mSampled.mEyes.mWorld.mWidth;
        const std::uint32_t height = frame.mSampled.mEyes.mWorld.mHeight;
        clamp.record(commands,
            HistoryClampPass::Images{ .mSampled = buffer.get(Channel::Specular),
                .mMean = images.mMean,
                .mFastBlended = images.mFastBlended,
                .mFast = images.mFast },
            Shaders::HistoryClampConstants{ .mWidth = width,
                .mHeight = height,
                .mAntilag = frame.mFilters.mAntilag ? 1u : 0u,
                .mFrame = frame.mSampled.mFrame,
                .mLayer = Shaders::HISTORY_CLAMP_GLOSSY });
        return images.mMean;
    }
}
