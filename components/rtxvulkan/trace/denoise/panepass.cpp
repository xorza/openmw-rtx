#include "panepass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/pane.h>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::PANE_BINDINGS> sBindings
            = computeBindings<Shaders::PANE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    }

    PanePass::PanePass(const Device& device)
        : mPipeline(device, sBindings, {}, "pane.comp.spv", "pane")
    {
    }

    const Image& PanePass::record(VkCommandBuffer commands, const DenoiseHistory::PaneImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mCamera;
        const std::uint32_t width = camera.mWidth;
        const std::uint32_t height = camera.mHeight;
        assert(images.mMean.getWidth() >= width && images.mMean.getHeight() >= height);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::PANE_BIND_PANE, buffer.get(Channel::Pane).describeStorage());
        writes.image(Shaders::PANE_BIND_SURFACE, buffer.get(Channel::PaneSurface).describeStorage());
        writes.image(Shaders::PANE_BIND_MOTION, buffer.get(Channel::PaneMotion).describeStorage());
        writes.image(Shaders::PANE_BIND_HELD_BEFORE, images.mHeldBefore.describeStorage());
        writes.image(Shaders::PANE_BIND_HELD, images.mHeld.describeStorage());
        writes.image(Shaders::PANE_BIND_MEAN_BEFORE, images.mMeanBefore.describeStorage());
        writes.image(Shaders::PANE_BIND_MEAN, images.mMean.describeStorage());

        const Shaders::HistoryConstants constants{
            .mCamera = camera,
            .mReset = images.mFresh ? 1u : 0u,
            .mDistanceScale = frame.mDistanceScale,
        };

        dispatch(commands, mPipeline, writes, constants, Groups::covering(width, height, Shaders::PANE_WORKGROUP));

        images.mMean.transition(commands, Use::sComputeWrite, Use::sComputeRead);
        return images.mMean;
    }
}
