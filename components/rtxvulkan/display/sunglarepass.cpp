#include "sunglarepass.hpp"

#include <array>
#include <cstddef>
#include <span>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/glare.h>

namespace Rtx
{
    namespace
    {
        /// The counts in, the share out.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::SUN_GLARE_BINDINGS> sBindings
            = computeBindings<Shaders::SUN_GLARE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    SunGlarePass::SunGlarePass(const Device& device)
        : mPipeline(device, sBindings, {}, "sunglare.comp.spv", "sun glare")
        , mCounts(Buffer::deviceLocal(device, sizeof(Shaders::SunGlareCount),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "sun glare counts"))
        , mShare(Buffer::deviceLocal(device, sizeof(float),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "sun glare share"))
        , mNoShare(Buffer::deviceLocal(device, sizeof(float),
              VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "no sun glare"))
    {
        // Written once, before any frame, into video memory, as everything written once is.
        const float none = 0.0f;
        device.getPool().submitAndWait([&](VkCommandBuffer commands) {
            mNoShare.updateInline(commands, Use::sBufferAnyRead, std::as_bytes(std::span(&none, 1)));
        });
    }

    void SunGlarePass::begin(const VkCommandBuffer commands) const
    {
        // The previous frame's easing, which read the counts, is behind the head barrier
        // `CommandPool::begin` recorded; the launch that adds to them is behind the clear.
        mCounts.clear(commands);
        mCounts.transition(commands, Use::sBufferClearWrite, Use::sBufferComputeReadWrite);
    }

    void SunGlarePass::record(
        const VkCommandBuffer commands, const float elapsedSeconds, const bool reset, GpuTimer* timer) const
    {
        const GpuZone timed(timer, commands, FrameZone::Glare);

        // The counts the launch added to. The share this easing moves was last read by the
        // previous frame's curve and written by its easing, both behind the head barrier
        // `CommandPool::begin` recorded.
        mCounts.transition(commands, Use::sBufferComputeWrite, Use::sBufferComputeRead);

        DescriptorWrites writes(mPipeline);
        writes.buffer(Shaders::SUN_GLARE_BIND_COUNT, mCounts.describe());
        writes.buffer(Shaders::SUN_GLARE_BIND_SHARE, mShare.describe());

        const Shaders::SunGlareConstants constants{
            .mElapsed = elapsedSeconds,
            .mReset = reset ? 1u : 0u,
        };

        dispatch(commands, mPipeline, writes, constants, Groups{});

        // The curve reads what the easing wrote.
        mShare.transition(commands, Use::sBufferComputeWrite, Use::sBufferComputeRead);
    }
}
