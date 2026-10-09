#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWWORD_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHADOWWORD_GLSL

// A shadow field's mean and variance in one word, `SHADOW_REPROJECTED`: the mean, which is in
// `[0, 1]`, as a sixteen-bit unorm in the low half, and the variance as a half in the high half. And
// its moments in two, `SHADOW_MOMENTS`, with the update that makes them.
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

#include "shared/shadow.h"

#include "census.glsl"
#include "halfround.glsl"
#include "hash.glsl"

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

/// A field's moments, the SDK's three: the mean of the bits, the sum of their squared deviations
/// from it, and how many frames they count. And the variance this frame's update made of them.
struct ShadowMoments
{
    vec3 mMoments;
    float mVariance;
};

/// `previous` with this frame's bit `current` counted in: Welford's update, which is the SDK's, up to
/// `SHADOW_MOMENT_FRAMES`, and past it the same mean and sum over that many frames, the earlier ones'
/// sum scaled to the share the mean's weight leaves them. **So both become running means over the
/// cap's frames**, where Welford's sum grew with every frame and its count without end.
///
/// **The variance never past one, which is the SDK's own answer for a history of one sample**, and
/// more than a bit's variance can be: the count is damped for a history that stood off, so
/// `samples - 1` falls toward nought while the sum stays, and a variance of a million passed what
/// the filter's halves hold. Its infinity over its own square was a NaN at every level of the filter
/// after it, at every place `check` visits.
ShadowMoments updatedShadowMoments(vec3 previous, float current)
{
    const float samples = min(previous.z + 1.0, SHADOW_MOMENT_FRAMES);
    const float kept = previous.z > SHADOW_MOMENT_FRAMES - 1.0 ? (SHADOW_MOMENT_FRAMES - 1.0) / previous.z : 1.0;
    const float mean = previous.x + (current - previous.x) / samples;
    const float deviations = previous.y * kept + (current - previous.x) * (current - mean);
    const float variance = samples > 1.0 ? min(deviations / (samples - 1.0), 1.0) : 1.0;
    return ShadowMoments(vec3(mean, deviations, samples), variance);
}

/// `moments` in two words: the mean as a unorm in the first's low half and the sum as a half in its
/// high half, and the count as a half in the second's low half. **Each rounded at random by a draw
/// of its own from `draws`**, since the next frame's update reads all three back. A half's steps
/// stand in proportion to what it holds, so the sum needs no scale to keep its precision, however
/// few frames it counts; the cap holds the count, and the sum wherever the count reaches it, far
/// under the largest half, and a sum a damped count left larger reads a variance of one anyway.
uvec2 packShadowMoments(vec3 moments, inout uint draws)
{
    const uint mean = roundedToUnorm16(moments.x, randomWord(draws));
    const float deviations = roundedToHalf(moments.y, randomWord(draws));
    const float samples = roundedToHalf(moments.z, randomWord(draws));
    return uvec2(mean | (packHalf2x16(vec2(deviations, 0.0)) << 16u), packHalf2x16(vec2(samples, 0.0)));
}

vec3 unpackShadowMoments(uvec2 words)
{
    return vec3(float(words.x & 0xffffu) * UNORM16_STEP, unpackHalf2x16(words.x >> 16u).x, unpackHalf2x16(words.y).x);
}

#endif
