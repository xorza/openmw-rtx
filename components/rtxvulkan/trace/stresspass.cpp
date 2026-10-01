#include "stresspass.hpp"

#include <array>
#include <cmath>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/shaders/stress.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::STRESS_BINDINGS> sBindings
            = computeBindings<Shaders::STRESS_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    }

    StressPass::StressPass(const Device& device, const double milliseconds)
        : mPipeline(device, sBindings, {}, "stress.comp.spv", "stress")
        , mNanoseconds(static_cast<std::uint32_t>(std::llround(milliseconds * 1.0e6)))
    {
    }

    void StressPass::record(VkCommandBuffer commands, GpuTimer& timer, const Buffer& counts)
    {
        // A literal, so its view is terminated.
        timer.open(commands, RenderProfile::sHoldZone.data());

        DescriptorWrites writes(mPipeline);
        writes.buffer(Shaders::STRESS_BIND_COUNTS, counts.describe());
        dispatch(commands, mPipeline, writes, Shaders::StressConstants{ .mNanoseconds = mNanoseconds }, Groups{});

        timer.close(commands);
    }
}
