#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOOM_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOOM_GLSL

// The two kernels the pyramid is built and taken apart with.
//
// **One copy, because three passes ask for them**: `bloomdown.comp` halving the frame and then each
// level, `bloomup.comp` spreading each level into the one above it, and `tone.comp` spreading the
// finest one over the picture. The last two run the same nine taps, and the display pass runs them
// because that is where the veil belongs — `BloomPass` says why nothing writes the frame.
//
// Both are Jorge Jimenez's, out of *Next Generation Post Processing in Call of Duty: Advanced
// Warfare* (SIGGRAPH 2014), and both lean on the sampler: every tap sits on a texel corner, so the
// hardware's bilinear fetch reads four texels for each one written here — thirteen taps for
// thirty-six texels going down, nine for thirty-six coming back up.

#include "colour.h"

/// The frame or a level, halved.
///
/// **Thirteen taps in five overlapping squares rather than one box.** A plain 2x2 box halved twice
/// is a wider box, and a wider box has a flat top and a sharp edge — which is what makes a cheap
/// bloom shimmer as a highlight crosses a texel boundary of a level nobody is looking at. The
/// overlap is what smooths the transfer between levels, and the corner weights are what taper it:
/// the inner square half of the whole, and each of the four outer squares an eighth.
///
/// **Each square weighed by `1 / (1 + luminance × exposure)` where `exposure` is not nought**, the
/// Karis average, on the frame's own halving and on no level after it (Jimenez 2014, after Karis
/// 2013). With no threshold every texel blooms, and a firefly the trace drew for one frame alone is
/// as bright as anything in the frame: averaged plainly it is a disc up to sixty-four pixels across
/// that pulses with it. The weight is the exposure's, so a square the curve shows at mid grey
/// keeps most of its say whatever the light's own scale, and one far over white keeps almost none.
/// Nought weighs every square alike, which is the plain average.
///
/// @param uv the corner the destination texel stands on in the source, `BloomConstants::mTexel`.
/// @param texel one source texel in source texture coordinates.
/// @param exposure what the curve scales the frame by, or nought for no Karis average.
vec3 bloomHalved(sampler2D source, vec2 uv, vec2 texel, float exposure)
{
    const vec2 wide = 2.0 * texel;

    const vec3 a = textureLod(source, uv + vec2(-wide.x, wide.y), 0.0).rgb;
    const vec3 b = textureLod(source, uv + vec2(0.0, wide.y), 0.0).rgb;
    const vec3 c = textureLod(source, uv + wide, 0.0).rgb;

    const vec3 d = textureLod(source, uv + vec2(-wide.x, 0.0), 0.0).rgb;
    const vec3 e = textureLod(source, uv, 0.0).rgb;
    const vec3 f = textureLod(source, uv + vec2(wide.x, 0.0), 0.0).rgb;

    const vec3 g = textureLod(source, uv - wide, 0.0).rgb;
    const vec3 h = textureLod(source, uv + vec2(0.0, -wide.y), 0.0).rgb;
    const vec3 i = textureLod(source, uv + vec2(wide.x, -wide.y), 0.0).rgb;

    const vec3 j = textureLod(source, uv + vec2(-texel.x, texel.y), 0.0).rgb;
    const vec3 k = textureLod(source, uv + texel, 0.0).rgb;
    const vec3 l = textureLod(source, uv - texel, 0.0).rgb;
    const vec3 m = textureLod(source, uv + vec2(texel.x, -texel.y), 0.0).rgb;

    const vec3 inner = (j + k + l + m) * 0.25;
    const vec3 upLeft = (a + b + d + e) * 0.25;
    const vec3 upRight = (b + c + e + f) * 0.25;
    const vec3 downLeft = (d + e + g + h) * 0.25;
    const vec3 downRight = (e + f + h + i) * 0.25;

    const float innerWeight = 0.5 / (1.0 + max(dot(inner, LUMINANCE_WEIGHTS), 0.0) * exposure);
    const vec4 outerWeights = 0.125
        / (1.0
            + max(vec4(dot(upLeft, LUMINANCE_WEIGHTS), dot(upRight, LUMINANCE_WEIGHTS), dot(downLeft, LUMINANCE_WEIGHTS),
                      dot(downRight, LUMINANCE_WEIGHTS)),
                  0.0)
                * exposure);

    const vec3 weighed = inner * innerWeight + upLeft * outerWeights.x + upRight * outerWeights.y
        + downLeft * outerWeights.z + downRight * outerWeights.w;
    return weighed / (innerWeight + dot(outerWeights, vec4(1.0)));
}

/// A level, spread over the one above it.
///
/// @param uv where the destination texel's centre lands in the source: `(q + ½) / 2` source texels,
///        `BloomConstants::mTexel`.
///
/// A 3x3 tent, which is the widest kernel a doubling can carry without reaching past the texels the
/// level below actually holds.
///
/// @param texel one source texel, so the tent is a fixed shape in the coarser grid and twice the
///        span in the finer one.
vec3 bloomSpread(sampler2D source, vec2 uv, vec2 texel)
{
    const vec3 a = textureLod(source, uv + vec2(-texel.x, texel.y), 0.0).rgb;
    const vec3 b = textureLod(source, uv + vec2(0.0, texel.y), 0.0).rgb;
    const vec3 c = textureLod(source, uv + texel, 0.0).rgb;

    const vec3 d = textureLod(source, uv + vec2(-texel.x, 0.0), 0.0).rgb;
    const vec3 e = textureLod(source, uv, 0.0).rgb;
    const vec3 f = textureLod(source, uv + vec2(texel.x, 0.0), 0.0).rgb;

    const vec3 g = textureLod(source, uv - texel, 0.0).rgb;
    const vec3 h = textureLod(source, uv + vec2(0.0, -texel.y), 0.0).rgb;
    const vec3 i = textureLod(source, uv + vec2(texel.x, -texel.y), 0.0).rgb;

    return (e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i)) * (1.0 / 16.0);
}

#endif
