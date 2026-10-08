#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HALFROUND_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HALFROUND_GLSL

// A float rounded to a half by the shader, at random, for a history kept in halves.
//
// **A value a half holds exactly is stored as itself whatever the store's rounding**, which Vulkan
// leaves to the device and this card makes toward nought (`RtxHalfStoreTest`). So the store is made
// exact here, and the rounding is chosen: **at random, up with the chance of the share of the step
// the value stands above the half below it**, so the stored value's mean is the value. A running
// mean rounded toward nought falls a little at every store; one rounded to nearest stops moving
// anywhere within half a step over its blend's weight of its target, which a still surface under a
// steady light never leaves. Rounded at random, it moves by its target's whole difference on
// average, and the cost is noise of half a step at most, about a part in two thousand at a store:
// stochastic rounding (Croci et al. 2022; Connolly, Higham and Mary 2021).
//
// **Every step exact**: `frexp`, `ldexp`, `floor` and `fract`, and products with powers of two, which
// the specification rounds exactly and the pin leaves nothing to — so every device stores the same
// half from the same float and draw.

#include "census.glsl"
#include "hash.glsl"

/// The largest finite half.
const float HALF_LARGEST = 65504.0;

/// `value` as a half holds it, rounded up or down by `draw`, one number in `[0, 1)` from a sequence
/// of the caller's own (`SEED_HALF_ROUNDING`). Under the least normal half the step is the
/// subnormal one, 2^-24; past the largest, the largest.
float roundedToHalf(float value, float draw)
{
    countNotFinite(value);
    const float held = min(abs(value), HALF_LARGEST);

    int exponent;
    frexp(held, exponent);
    const int stepExponent = max(exponent - 1, -14) - 10;
    const float steps = held * ldexp(1.0, -stepExponent);
    const float below = floor(steps);
    const float rounded = ldexp(below + (fract(steps) > draw ? 1.0 : 0.0), stepExponent);
    return sign(value) * min(rounded, HALF_LARGEST);
}

/// `roundedToHalf` in each channel, each by a draw of its own from `draws`.
vec4 roundedToHalf(vec4 value, inout uint draws)
{
    const float x = roundedToHalf(value.x, randomNext(draws));
    const float y = roundedToHalf(value.y, randomNext(draws));
    const float z = roundedToHalf(value.z, randomNext(draws));
    return vec4(x, y, z, roundedToHalf(value.w, randomNext(draws)));
}

#endif
