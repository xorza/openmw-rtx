#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWWORD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWWORD_GLSL

// A shadow field's mean and variance in one word, `SHADOW_REPROJECTED`: the mean, which is in
// `[0, 1]`, as a sixteen-bit unorm in the low half, and the variance as a half in the high half.
//
// **The mean rounded by the shader, and at random where a blend reads it back**, for the reason
// `halfround.glsl` gives: a running mean rounded to the nearest step stops anywhere within half a
// step over its blend's weight of its target, ten steps at the temporal pass's five per cent, and
// a penumbra under a still light never leaves that. Rounded at random, its noise is half a step,
// 1/131070, at most.
//
// **In integers on the float's bits**, so the chance a value rounds up is the share of the step it
// stands above the unorm below it on every device: the float's twenty-four-bit significand times
// 65535 is made whole in two words, and the share is read off its bits under the step, to the
// twenty-four bits the random word has. A value under 2^-40 is nought: its share is under the
// word's least bit.
//
// **The variance to the nearest half**: no frame reads it back, since the temporal pass makes
// its own from the moments, so a rounding of it never builds. `SHADOW_NO_RECEIVER` is a half
// exactly.

#include "census.glsl"
#include "halfround.glsl"

/// What a unorm's step is multiplied back by, 2^-16 + 2^-32: 1/65535 rounded to a float, and so
/// the product that lands on 1 from 65535, since that product is `1 - 2^-32`. Nought and one come
/// back exact, as a cleared tile's must, and every step between within an ulp of its value.
const float UNORM16_STEP = (1.0 + 1.0 / 65536.0) / 65536.0;

/// The random word that rounds a value to the nearest step, a half step up: a share rounds up
/// past it, and the word's twenty-four bits hold the share to the bit.
const uint UNORM16_NEAREST = 0x7fffffu;

/// The low word of `high:low` shifted right by `by`, from nought to 63: GLSL leaves a shift of a
/// word by 32 or more undefined.
uint shiftedRight(uint high, uint low, uint by)
{
    return by >= 32u ? high >> (by - 32u) : (by == 0u ? low : (low >> by) | (high << (32u - by)));
}

/// `value` as a sixteen-bit unorm, up from the step below with the chance of the share of the step
/// it stands above it, by `word`, twenty-four random bits (`randomWord`), or to the nearest by
/// `UNORM16_NEAREST`. Clamped to `[0, 1]`.
uint roundedToUnorm16(float value, uint word)
{
    countNotFinite(value);
    const uint bits = floatBitsToUint(clamp(value, 0.0, 1.0));

    // The value is `significand * 2^-shift`, a shift from 23 at one to 63 at 2^-40.
    const uint biased = bits >> 23u;
    const uint significand = (bits & 0x7fffffu) | 0x800000u;
    const uint shift = 150u - clamp(biased, 87u, 127u);

    uint high;
    uint low;
    umulExtended(significand, 65535u, high, low);
    const uint whole = shiftedRight(high, low, shift);
    const uint share = shift > 23u ? shiftedRight(high, low, shift - 24u) & 0xffffffu : 0u;
    return biased < 87u ? 0u : whole + (share > word ? 1u : 0u);
}

/// `value`, a mean in `x` and its variance in `y`, as one word: the mean by `roundedToUnorm16`
/// with `word`, and the variance as the nearest half, which a pack keeps exactly.
uint packShadowWord(vec2 value, uint word)
{
    return roundedToUnorm16(value.x, word) | (packHalf2x16(vec2(nearestHalf(value.y), 0.0)) << 16u);
}

vec2 unpackShadowWord(uint word)
{
    return vec2(float(word & 0xffffu) * UNORM16_STEP, unpackHalf2x16(word >> 16u).x);
}

#endif
