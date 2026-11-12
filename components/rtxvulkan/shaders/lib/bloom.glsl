#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOOM_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLOOM_GLSL

// The two kernels the pyramid is built and taken apart with.
//
// One copy, because three passes ask for them: `bloomdown.comp`, `bloomup.comp` and `tone.comp`.

#include "colour.h"

/// The frame or a level, halved.
///
/// Thirteen taps in five overlapping squares rather than one box: a plain 2x2 box halved twice is a wider
/// box with a flat top and sharp edge, which makes a cheap bloom shimmer as a highlight crosses a texel
/// boundary. The overlap smooths the transfer between levels and the corner weights taper it: the inner
/// square half of the whole, each of the four outer squares an eighth.
///
/// Each square is weighed by `1 / (1 + luminance × exposure)` where `exposure` is not nought, the Karis
/// average, on the frame's own halving and on no level after it (Jimenez 2014, after Karis 2013).
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
