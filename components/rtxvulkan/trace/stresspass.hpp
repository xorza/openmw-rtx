#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/stress.h>

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// A dispatch that holds the queue for a stated time and touches nothing a frame reads —
    /// `RenderProfile::mStressOverlapMs`. Appended to every frame's trace, it keeps the device
    /// that far behind the host, so every frame is recorded over a frame still running: a hazard
    /// that needs the overlap to show shows on the first frame of every run rather than on one run
    /// in four. The zone it is timed as is `FrameZone::Stress`, so the report shows what it
    /// actually held.
    ///
    /// **The time is measured where it passes, in the loop, off the device's real-time clock.**
    /// `stress.comp` says why a count is not a time on a card whose clock moves, and it moves the
    /// most on the frames a run measures first. What the loop's clock came to is left in the
    /// frame's counts, `Shaders::FrameCounts::mHeldTicks`, which the ring reads back once the frame
    /// is waited for — `FrameResult::mHeldMs`.
    ///
    /// **The clock's tick is the queue's timestamp period.** `GL_EXT_shader_realtime_clock` names
    /// no unit, and a hold asked in nanoseconds would run ten times as long on RDNA and report a
    /// tenth of it. Both target vendors read one counter for the two: NVIDIA's
    /// `%globaltimer` counts nanoseconds and its period is one; RDNA's `s_memrealtime` is the
    /// 100 MHz reference clock, which RADV states as a period of `1e6 / clock_crystal_freq`
    /// nanoseconds. Timing the loop against the queue's timestamps instead read stalls of up to
    /// 2.6 ms around a loop on a card stepping its clock, and moved the rate by four fifths.
    class StressPass
    {
    public:
        /// @param milliseconds how long every frame's hold is to be.
        StressPass(const Device& device, double milliseconds);

        /// The milliseconds one tick of the loop's clock is worth.
        double getTickMs() const { return mTickMs; }

        /// The ticks every hold asks for.
        std::uint32_t getTicks() const { return mTicks; }

        /// Records the hold into `commands`, timed as `FrameZone::Stress`, leaving what the
        /// loop's clock read in `counts`: the frame's own block, so the reading is the frame's and
        /// not whichever frame in flight wrote last.
        void record(VkCommandBuffer commands, GpuTimer& timer, const Buffer& counts);

    private:
        ComputePipeline<Shaders::StressConstants> mPipeline;

        double mTickMs = 0.0;
        std::uint32_t mTicks = 0;
    };
}
