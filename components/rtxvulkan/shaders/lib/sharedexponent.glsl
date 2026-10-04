#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHAREDEXPONENT_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_SHAREDEXPONENT_GLSL

// A colour in one word, for what a pass keeps of many pixels and reads many times: the bounce's
// reservoirs and the accumulator's fast means. **Rounded to nearest, where a half store rounds toward
// nought on this card** (`RtxHalfStoreTest`), so a running mean kept in it does not fall a little at
// every store.

/// The largest value `RGB9E5` holds: nine bits of mantissa at the largest of its exponents.
const float RGB9E5_LARGEST = 65408.0;

/// A colour as three nine-bit mantissas sharing one five-bit exponent: the one each channel needs
/// for the brightest of them. **What a radiance loses is relative to its brightest channel**, a
/// part in a thousand at worst, where three halves would cost the same six bytes twice over.
/// Anything not a number, or below nought, is nought.
uint packRgb9e5(vec3 colour)
{
    const vec3 held = clamp(mix(colour, vec3(0.0), isnan(colour)), vec3(0.0), vec3(RGB9E5_LARGEST));
    const float brightest = max(held.r, max(held.g, held.b));

    // The exponent that puts the brightest channel just under one at nine bits, held to what five
    // bits store; one more where rounding carries it to the next power.
    int exponent;
    frexp(brightest, exponent);
    exponent = max(exponent, -15);
    uvec3 mantissa = uvec3(round(held * exp2(float(9 - exponent))));
    if (max(mantissa.r, max(mantissa.g, mantissa.b)) == 512u)
    {
        ++exponent;
        mantissa = uvec3(round(held * exp2(float(9 - exponent))));
    }

    return mantissa.r | (mantissa.g << 9u) | (mantissa.b << 18u) | (uint(exponent + 15) << 27u);
}

vec3 unpackRgb9e5(uint packed)
{
    const uvec3 mantissa = uvec3(packed, packed >> 9u, packed >> 18u) & 0x1FFu;
    return vec3(mantissa) * exp2(float(int(packed >> 27u) - 15 - 9));
}

#endif
