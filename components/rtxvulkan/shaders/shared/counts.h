#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_COUNTS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_COUNTS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What a frame counts on the device for the host to read once the frame is waited for. One block
// for the frame, so the frame that clears it, the passes that write it and the ring that reads it
// back agree about where each word sits. Included verbatim by both sides, for the reason
// `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    struct FrameCounts
    {
        /// Primary rays that reached nothing, summed by the sky's miss shader where the trace was
        /// built to count — `COUNTING`. The host reports the hits, which are the launch less
        /// these: `FrameRing::finishOldest`.
        uint mMisses;

        /// What the hold's own clock said the hold came to, in its ticks, written by the loop
        /// `check` appends to the frame — `stress.comp`. Left alone by a frame with no hold.
        uint mHeldTicks;
    };

    /// How many shader modules the census tells apart: every module the backend builds
    /// (`RtxSourceTreeTest` counts them), and room past them.
    const uint CENSUS_KERNELS = 96u;

    /// Where a census module binds the census in every pass's own set (`SET_PASS`): past every
    /// binding a pass numbers for itself, which count up from nought. Only in the census's modules
    /// and only in the layouts of a device that counts (`PipelineLayout`).
    const uint BIND_CENSUS = 31u;

    /// The module's own word of the census, which `Rtx::Specialization` hands every stage a
    /// counting device makes: numbered past every pass's own constants, which count up from nought.
    const uint SPEC_CENSUS_KERNEL = 1000u;

    /// **Stores whose value was a NaN or an infinity, by the shader module that made them**,
    /// summed by `countNotFinite` in the census's modules: one census for the device, which a frame
    /// copies out and clears as it ends (`NotFiniteCensus`). Every one is a logic error, which nothing refuses: a
    /// history that takes one keeps it and spreads it, as a froxel that took `0 / 0` once spread across the whole frame
    /// in eight-pixel blocks. `Check::Finite` asserts nought over a stop.
    struct Census
    {
        uint mNotFinite[CENSUS_KERNELS];
    };

#ifdef RTX_HOST
    static_assert(sizeof(FrameCounts) == 8, "FrameCounts must be scalar-packed on every side");
    static_assert(sizeof(Census) == 4 * CENSUS_KERNELS, "Census must be scalar-packed on every side");
}
#endif

#endif
