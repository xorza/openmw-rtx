#include "panepass.hpp"

#include <array>
#include <cassert>
#include <cstdint>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/pane.h>
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
        const GBuffer& buffer, const DenoiseFrame& frame, const HistoryClampPass& clamp, GpuTimer* timer) const
    {
        const GpuZone timed(timer, commands, FrameZone::Pane);

        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
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
        writes.image(Shaders::PANE_BIND_FAST_BEFORE, images.mFast.describeStorage());
        writes.image(Shaders::PANE_BIND_FAST_BLENDED, images.mFastBlended.describeStorage());

        const Shaders::HistoryConstants constants = frame.history(images.mFresh);

        dispatch(commands, mPipeline, writes, constants, Groups::covering(width, height, Shaders::PANE_WORKGROUP));
        clamp.record(commands,
            HistoryClampPass::Images{ .mSampled = buffer.get(Channel::Pane),
                .mMean = images.mMean,
                .mFastBlended = images.mFastBlended,
                .mFast = images.mFast },
            width, height, frame.mFilters.mAntilag, frame.mSampled.mFrame);
        return images.mMean;
    }
}
