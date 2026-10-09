#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UNORMROUND_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_UNORMROUND_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What the unorm-rounding probe is handed: floats and random words, for `RtxHalfStoreTest` to read
// back how `lib/shadowword.glsl` rounds and packs each. Included verbatim by both sides, for the
// reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Threads in the probe's workgroup.
    const uint UNORM_ROUND_WORKGROUP = 64;

    /// The probe's bindings: the floats and the words in; each float rounded by its word
    /// (`roundedToUnorm16`); the mean the word that is the entry's index reads as
    /// (`unpackShadowWord`); and each float packed as a variance and read back, beside it rounded to
    /// the nearest half (`nearestHalf`).
    const uint UNORM_ROUND_BIND_VALUES = 0;
    const uint UNORM_ROUND_BIND_WORDS = 1;
    const uint UNORM_ROUND_BIND_ROUNDED = 2;
    const uint UNORM_ROUND_BIND_MEANS = 3;
    const uint UNORM_ROUND_BIND_VARIANCES = 4;
    const uint UNORM_ROUND_BIND_NEAREST = 5;

    struct UnormRoundConstants
    {
        /// How many floats there are.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(sizeof(UnormRoundConstants) == 4, "UnormRoundConstants must be scalar-packed on every side");
}
#endif

#endif
