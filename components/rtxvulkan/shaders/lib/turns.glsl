#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TURNS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_TURNS_GLSL

#include "scene.h"

/// How far into its turn something that turns `rate` times a second is, `seconds` after nought: the
/// fraction of `rate * seconds`, from nought up to one.
///
/// What rounding took off `product`, the float nearest `a b`: exactly `a b - product`, by Dekker's
/// product (Dekker 1971, *A floating-point technique for extending the available precision*), which
/// needs no fused multiply-add. Veltkamp's split by `4097 x` leaves each factor a high half of twelve
/// bits and a low half of the rest, so every partial product is a float exactly. Exact while
/// `4097 a` and `4097 b` are finite and nothing is subnormal.
float productError(float a, float b, float product)
{
    const float splitA = 4097.0 * a;
    const float highA = splitA - (splitA - a);
    const float lowA = a - highA;
    const float splitB = 4097.0 * b;
    const float highB = splitB - (splitB - b);
    const float lowB = b - highB;
    return ((highA * highB - product) + highA * lowB + lowA * highB) + lowA * lowB;
}

/// **Exact to a float's last place however long the clock has run.** The clock arrives as two floats
/// whose sum is the host's double, `Rtx::splitSeconds`. The product of the larger half is its rounded
/// value plus an error `productError` recovers exactly, and the fraction of a float is exact, so the
/// only rounding left is on numbers under one. A phase taken as `rate * seconds` in one float drifts
/// by the clock's own rounding times the rate: a quarter of a radian of a wave after a hundred hours.
float turnsAt(float rate, vec2 seconds)
{
    const float whole = rate * seconds.x;
    const float lost = productError(rate, seconds.x, whole);
    return fract(fract(whole) + lost + rate * seconds.y);
}

/// `(cos, sin)` of `turns` whole turns, for `turns` from nought up to one.
///
/// **The quarter turns are exact and only the rest goes to `sin` and `cos`**, which Vulkan bounds
/// only inside `[-PI, PI]`, and `TAU` times a phase in `[0, 1)` reaches `2 PI`. The nearest
/// quarter is a multiply by a power of `i`, which swaps and negates, and the rest, within an eighth
/// of a turn of it, is subtracted exactly: four times a float is exact, and so is the difference of
/// two floats within a factor of two of each other.
vec2 phasorAt(float turns)
{
    const float quarters = round(4.0 * turns);
    const float angle = TAU * (turns - 0.25 * quarters);
    const vec2 near = vec2(cos(angle), sin(angle));

    const uint power = uint(quarters) & 3u;
    const vec2 turned = (power & 1u) != 0u ? vec2(-near.y, near.x) : near;
    return (power & 2u) != 0u ? -turned : turned;
}

#endif
