#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHAREDEXPONENT_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHAREDEXPONENT_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the shared-exponent probe is handed: colours, for `RtxSharedExponentTest` to read back the
// word `lib/sharedexponent.glsl` packs each into and the colour it unpacks again. Included verbatim
// by both sides, for the reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint SHARED_EXPONENT_WORKGROUP = 64;

    /// The probe's bindings: the colours in, each packed (`packRgb9e5`), and each word unpacked
    /// again (`unpackRgb9e5`).
    const uint SHARED_EXPONENT_BIND_COLOURS = 0;
    const uint SHARED_EXPONENT_BIND_PACKED = 1;
    const uint SHARED_EXPONENT_BIND_UNPACKED = 2;

    struct SharedExponentConstants
    {
        /// How many colours there are.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(SharedExponentConstants) == 4, "SharedExponentConstants must be scalar-packed on every side");
}
#endif

#endif
