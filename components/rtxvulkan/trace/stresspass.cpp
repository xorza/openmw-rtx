#include "stresspass.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/stress.h>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::STRESS_BINDINGS> sBindings
            = computeBindings<Shaders::STRESS_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

        std::uint32_t ticksFor(const PhysicalDevice& device, const double milliseconds)
        {
            const auto period
                = static_cast<double>(device.getProperties().mProperties2.properties.limits.timestampPeriod);
            assert(period > 0.0 && "a hold needs a clock that advances");

            // The quotient rounds before the ceiling, so it can stand a tick either side of the
            // least count; the steps settle it against the one reading every check takes.
            auto ticks = static_cast<std::uint64_t>(std::ceil(milliseconds * 1.0e6 / period));
            while (ticks > 0 && device.timestampMs(ticks - 1) >= milliseconds)
                --ticks;
            while (device.timestampMs(ticks) < milliseconds)
                ++ticks;

            assert(ticks <= std::numeric_limits<std::uint32_t>::max() && "a hold past the loop's 32-bit count");
            return static_cast<std::uint32_t>(ticks);
        }
    }

    StressPass::StressPass(const Device& device, const double milliseconds)
        : mPipeline(device, sBindings, {}, "stress.comp.spv", "stress")
        , mTicks(ticksFor(device.getPhysicalDevice(), milliseconds))
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
