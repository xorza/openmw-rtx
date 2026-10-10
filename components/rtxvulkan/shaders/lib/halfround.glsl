#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HALFROUND_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_HALFROUND_GLSL

// A float rounded to a half by the shader: at random for a history kept in halves, and to the
// nearest for a store no blend reads back.
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
// **A draw is a sample like any of the trace's**, so another draw moves a filtered picture where the
// filters' decisions amplify a part in two thousand: another glossy rounding seed moved 13 of
// `seyda-neen-ship-west`'s two million pixels by more than a level of 255, and none by more than 7,
// where another seed of the bounce's lobe moved 664,734, and one by 166.
//
// **In integers on the float's bits**, as `visibility.rgen` rounds the pane albedo: the half drops
// the float's low bits, so a number under them added and the bits cleared rounds up with exactly
// the share they hold, and half of them added, with the lowest kept bit, rounds to the nearest.
// Exact on every device, in a few operations where `frexp` and `ldexp` took many: against that
// float form, each narrow wavelet level measured 7% faster, the first 4 to 5%, and the frame 0.07
// to 0.09 ms on the default suite.

#include "census.glsl"
#include "hash.glsl"

/// The largest finite half.
const float HALF_LARGEST = 65504.0;

/// The least subnormal half, 2^-24, half of it, and 2^48, which scales a magnitude under the least
/// subnormal to a twenty-four-bit word's: each exact.
const float HALF_LEAST = 5.9604644775390625e-8;
const float HALF_LEAST_HALF = 2.98023223876953125e-8;
const float HALF_LEAST_TO_WORD = 281474976710656.0;

/// How many of `bits`' low bits, a float no larger than `HALF_LARGEST`, a half drops: thirteen for a
/// normal half, and one more for each binade under the least normal, 2^-14, down to every bit of
/// the fraction at 2^-24. A float under 2^-24, which a half holds as nought or 2^-24, is its
/// caller's.
uint droppedBits(uint bits)
{
    return uint(clamp(126 - int((bits >> 23u) & 0xffu), 13, 23));
}

/// `value` as a half holds it, rounded up or down by `word`, twenty-four bits from a sequence of the
/// caller's own (`randomWord`, `SEED_BOUNCE_ROUNDING` and those beside it). Past the largest half,
/// the largest.
float roundedToHalf(float value, uint word)
{
    countNotFinite(value);
    const float held = clamp(value, -HALF_LARGEST, HALF_LARGEST);
    const uint bits = floatBitsToUint(held);
    const uint dropped = (1u << droppedBits(bits)) - 1u;
    const float rounded = uintBitsToFloat((bits + (word & dropped)) & ~dropped);

    // Under 2^-24, up to it with the chance `|held| / 2^-24`, which is `|held| 2^48` against the
    // word: both sides exact.
    const float least = abs(held) * HALF_LEAST_TO_WORD > float(word) ? HALF_LEAST : 0.0;
    return abs(held) < HALF_LEAST ? (held < 0.0 ? -least : least) : rounded;
}

/// A light and a share of it, `whole` and `part` — the bounce and its fill — each rounded at random,
/// **every colour channel of the two by one draw** and the fourth of each by one of its own. A share
/// the same as its whole rounds the same, and one under it never rounds over it, since the rounding
/// by one draw is monotonic: what the composite takes off by one albedo and puts back by another
/// stays the same light.
void roundedToHalves(inout vec4 whole, inout vec4 part, inout uint draws)
{
    const uint x = randomWord(draws);
    const uint y = randomWord(draws);
    const uint z = randomWord(draws);
    const uint wholeFourth = randomWord(draws);
    const uint partFourth = randomWord(draws);
    whole = vec4(roundedToHalf(whole.x, x), roundedToHalf(whole.y, y), roundedToHalf(whole.z, z),
        roundedToHalf(whole.w, wholeFourth));
    part = vec4(roundedToHalf(part.x, x), roundedToHalf(part.y, y), roundedToHalf(part.z, z),
        roundedToHalf(part.w, partFourth));
}

/// `value` as a half holds it, rounded to the nearest, a tie to the even one: for a store no blend
/// reads back, a wavelet level's after the first, where rounding at random adds noise for a bias
/// that never builds.
float nearestHalf(float value)
{
    countNotFinite(value);
    const float held = clamp(value, -HALF_LARGEST, HALF_LARGEST);
    const uint bits = floatBitsToUint(held);
    const uint shift = droppedBits(bits);
    const uint dropped = (1u << shift) - 1u;
    const float rounded = uintBitsToFloat((bits + (dropped >> 1u) + ((bits >> shift) & 1u)) & ~dropped);

    // Under 2^-24, nought or 2^-24, the tie at 2^-25 to nought, the even one.
    const float least = abs(held) > HALF_LEAST_HALF ? HALF_LEAST : 0.0;
    return abs(held) < HALF_LEAST ? (held < 0.0 ? -least : least) : rounded;
}

/// `value` as a half holds it, each channel rounded toward nought by clearing the thirteen bits a
/// half drops: **only for a value between the least normal half, 2^-14, and `HALF_LARGEST`**, which
/// the caller holds it to, since under 2^-14 a half drops more and past the largest it holds none.
/// For a factor two shaders must meet to the bit, the light divided by it and the channel that
/// multiplies it back: every store of a half keeps what this leaves exactly. **By its bits, and not
/// by `unpackHalf2x16(packHalf2x16(x))`**, which this card's driver folds back to `x`: a pane's
/// albedo of 0.129 stored as 0.12890625 left its first filtered frame 0.07% darker than the frame the
/// trace composed.
vec3 truncatedToNormalHalf(vec3 value)
{
    return uintBitsToFloat(floatBitsToUint(value) & 0xffffe000u);
}

vec4 nearestHalf(vec4 value)
{
    const vec2 xy = vec2(nearestHalf(value.x), nearestHalf(value.y));
    return vec4(xy, nearestHalf(value.z), nearestHalf(value.w));
}

/// `roundedToHalf` in each channel, each by a draw of its own from `draws`.
vec2 roundedToHalf(vec2 value, inout uint draws)
{
    const float x = roundedToHalf(value.x, randomWord(draws));
    return vec2(x, roundedToHalf(value.y, randomWord(draws)));
}

vec4 roundedToHalf(vec4 value, inout uint draws)
{
    const float x = roundedToHalf(value.x, randomWord(draws));
    const float y = roundedToHalf(value.y, randomWord(draws));
    const float z = roundedToHalf(value.z, randomWord(draws));
    return vec4(x, y, z, roundedToHalf(value.w, randomWord(draws)));
}

#endif
