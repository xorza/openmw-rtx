#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADOWMOMENTS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADOWMOMENTS_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the shadow-moments probe is handed: a run of bits for each lane and a damping of the count
// for each frame, for `RtxShadowWordTest` to read back where `updatedShadowMoments` takes each lane's
// moments, kept as they are or packed and read back each frame as the temporal pass keeps them.
// Included verbatim by both sides, for the reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint SHADOW_MOMENTS_PROBE_WORKGROUP = 64;

    /// The probe's bindings: each frame's bits, lane after lane; each frame's damping of the count;
    /// and each lane's moments after the last frame and the variance that frame made, four floats.
    const uint SHADOW_MOMENTS_PROBE_BIND_BITS = 0;
    const uint SHADOW_MOMENTS_PROBE_BIND_DAMPING = 1;
    const uint SHADOW_MOMENTS_PROBE_BIND_MOMENTS = 2;

    struct ShadowMomentsProbeConstants
    {
        uint mLanes;
        uint mFrames;

        /// Non-zero where the moments are packed and read back each frame.
        uint mPacked;
    };

#ifdef RTX_HOST
    static_assert(
        sizeof(ShadowMomentsProbeConstants) == 12, "ShadowMomentsProbeConstants must be scalar-packed on every side");
}
#endif

#endif
