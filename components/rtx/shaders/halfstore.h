#ifndef OPENMW_COMPONENTS_RTX_SHADERS_HALFSTORE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_HALFSTORE_H

#include "hosttypes.h"
#include "portable.h"

// What the half-store probe is handed. Included verbatim by both sides, for the reason
// `visibility.h` is.
//
// **The probe is the question every history kept in halves rests on**: whether an image store into
// a half-float format rounds to nearest or toward nought. Vulkan allows either, and a running mean
// kept in halves stalls under the one and drifts down under the other (`accumulate.h`,
// `specular.h`).

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint HALF_STORE_WORKGROUP = 64;

    /// The probe's bindings: the floats in, the half-float image out.
    const uint HALF_STORE_BIND_VALUES = 0;
    const uint HALF_STORE_BIND_STORED = 1;

    struct HalfStoreConstants
    {
        /// How many floats are stored, one a texel along the image's first row.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(HalfStoreConstants) == 4, "HalfStoreConstants must be scalar-packed on every side");
}
#endif

#endif
