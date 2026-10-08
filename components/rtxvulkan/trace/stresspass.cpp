#include "stresspass.hpp"

#include <array>
#include <cmath>
#include <cstdint>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/stress.h>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::STRESS_BINDINGS> sBindings
            = computeBindings<Shaders::STRESS_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    StressPass::StressPass(const Device& device, const double milliseconds)
        : mPipeline(device, sBindings, {}, "stress.comp.spv", "stress")
        , mTickMs(static_cast<double>(
                      device.getPhysicalDevice().getProperties().mProperties2.properties.limits.timestampPeriod)
              * 1.0e-6)
        , mTicks(static_cast<std::uint32_t>(std::llround(milliseconds / mTickMs)))
    {
    }

    void StressPass::record(VkCommandBuffer commands, const Buffer& counts, GpuTimer* timer)
    {
        const GpuZone timed(timer, commands, FrameZone::Stress);

        DescriptorWrites writes(mPipeline);
        writes.buffer(Shaders::STRESS_BIND_COUNTS, counts.describe());
        dispatch(commands, mPipeline, writes, Shaders::StressConstants{ .mTicks = mTicks }, Groups{});
    }
}
