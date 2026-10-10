#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHARE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHARE_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the share probe is handed: floats, for `RtxShareTest` to read back what `lib/share.glsl`
// makes of each as a term and as a term added to itself. Included verbatim by both sides, for the
// reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint SHARE_PROBE_WORKGROUP = 64;

    /// The probe's bindings: the floats in, each as a term (`sharePart`), and each term added to
    /// itself (`addShare`).
    const uint SHARE_PROBE_BIND_VALUES = 0;
    const uint SHARE_PROBE_BIND_TERMS = 1;
    const uint SHARE_PROBE_BIND_SUMS = 2;

    struct ShareProbeConstants
    {
        /// How many floats there are.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(ShareProbeConstants) == 4, "ShareProbeConstants must be scalar-packed on every side");
}
#endif

#endif
