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
// **Exact on every device**, so the chance a value rounds up is the share of the step it stands
// above the unorm below it, to the twenty-four bits the random word has (`roundedToUnorm16`).
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

/// The word that rounds a value to the nearest step, a half up: its threshold is a half exactly.
const uint UNORM16_NEAREST = 0x7fffffu;

/// `value` as a sixteen-bit unorm, up from the step below where the share of the step it stands
/// above it is at least `(word + 1) 2^-24`: at random by `word`, twenty-four random bits
/// (`randomWord`), with the chance of that share to its twenty-four bits; and to the nearest, a half
/// up, by `UNORM16_NEAREST`. Clamped to `[0, 1]`.
///
/// **Exact, in a few floats**: the pinned `fma` gives what rounding `value × 65535` took off it,
/// so the share is the product's fraction and that loss. Where the product is at least a half, the
/// fraction and the threshold are both whole 2^-24ths under one, and their difference is exact; under
/// a half, it is exact by Sterbenz's lemma wherever it is near nought, and too far from it elsewhere
/// for its rounding to cross the loss. A product rounded up onto a whole number stands under it,
/// in the step below. **In place of the twenty-four-bit significand times 65535 made whole in two
/// words**, the same answer, whose two shifts across a word cost the first filter level 0.001 ms
/// more and the second 0.0015, both fields at 1280 by 720.
uint roundedToUnorm16(float value, uint word)
{
    countNotFinite(value);
    const float held = clamp(value, 0.0, 1.0);
    const float scaled = held * 65535.0;
    const float lost = fma(held, 65535.0, -scaled);
    const float whole = floor(scaled);
    const float share = scaled - whole;
    const float threshold = float(word + 1u) * (1.0 / 16777216.0);
    const bool under = share == 0.0 && lost < 0.0;
    const bool up = under ? 1.0 - threshold >= -lost : share - threshold >= -lost;
    return uint(whole) - (under ? 1u : 0u) + (up ? 1u : 0u);
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
