#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALFSTORE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALFSTORE_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the half-store probe is handed. Included verbatim by both sides, for the reason
// `visibility.h` is.
//
// **The probe is the question every history kept in halves rests on**: whether an image store into
// a half-float format rounds to nearest or toward nought. Vulkan allows either, and a running mean
// kept in halves stalls under the one and drifts down under the other (`accumulate.h`,
// `specular.h`). **And the two conversions beside it the spec leaves as open**: `packHalf2x16` in the
// arithmetic, and a store into a normalized eight-bit format, which the channels whose steps are a
// byte's rest on (`gbuffer.h`). **And the shader's own rounding to the nearest half**
// (`nearestHalf`), which a store then keeps exactly.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint HALF_STORE_WORKGROUP = 64;

    /// The probe's bindings: the floats in, the half-float image out, each float's `packHalf2x16`
    /// as a word, the eight-bit image out, and each float's `nearestHalf` as a float.
    const uint HALF_STORE_BIND_VALUES = 0;
    const uint HALF_STORE_BIND_STORED = 1;
    const uint HALF_STORE_BIND_PACKED = 2;
    const uint HALF_STORE_BIND_BYTES = 3;
    const uint HALF_STORE_BIND_NEAREST = 4;

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
