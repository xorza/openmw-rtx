#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALVING_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HALVING_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// How a level of a mip chain is made from the one above it: which texels each of its texels covers,
// and what each is worth. Read by both the device's passes that halve a texture, `mipchain.comp`
// and `normalspread.comp`, and by the host's chain a test holds the first to, so the three halve
// alike. Included
// verbatim by both sides, for the reason `visibility.h` is.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The taps along one axis: the first of three in a row, and their weights, which sum to one.
    struct AxisTaps
    {
        uint mFirst;
        vec3 mWeights;
    };

    /// The texels of the level above, along one axis `above` long, that texel `i` of the level under
    /// it covers: **the box of `above / here` texels laid over the axis.** Two halves where `above`
    /// is even; where it is odd, `2n + 1` over `n`, three taps worth `(n - i, n, i + 1) / (2n + 1)`
    /// (NVIDIA, *Non-Power-of-Two Mipmapping*). Halved as two, an odd axis's last texel was never
    /// read, and each level stood up to a texel of the one above off toward the far edge. An axis
    /// one texel long stays one. The weights past an axis's taps are nought, and the tap they name
    /// is the caller's to hold inside the level.
    RTX_SHADER AxisTaps axisTaps(uint i, uint above)
    {
        const float n = float(above / 2u);
        const float whole = float(above);
        AxisTaps taps;
        taps.mFirst = above == 1u ? 0u : 2u * i;
        taps.mWeights = above == 1u ? vec3(1.0f, 0.0f, 0.0f)
            : above % 2u == 0u      ? vec3(0.5f, 0.5f, 0.0f)
                                    : vec3((n - float(i)) / whole, n / whole, (float(i) + 1.0f) / whole);
        return taps;
    }

#ifdef RTX_HOST
}
#endif

#endif
