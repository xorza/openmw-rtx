// `#pragma once` everywhere else in this tree, and an include guard here for the reason
// `components/rtx/shaders/portable.h` gives.
#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RANDOM_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_RANDOM_GLSL

// Blue noise across the screen, a low-discrepancy sequence along time, and a hash for the
// one field that asks about a place in the world rather than a pixel on the screen.

#include "scene.h"
#include "bindings.glsl"

/// Three integers to one number in `[0, 1]`: a repeatable value for a cell of the noise field.
///
/// **The field's lattice and nothing else.** It fed the march's own jitter too, once; that draw
/// comes from the blue-noise tile now, because where a hash puts its error is nobody's decision and
/// a tile's is the whole point of it. What is left wants a hash and not a tile: `fogNoise` asks
/// about a place in the world rather than a pixel on the screen, and there is no screen-space
/// arrangement to arrange.
///
/// Sixteen bits is more than a haze needs, and taking the high ones is what keeps the low bits of a
/// weak avalanche out of the field.
float hashToUnit(ivec3 at)
{
    uvec3 wrapped = uvec3(at);
    uint h = wrapped.x * 1664525u + wrapped.y * 1013904223u + wrapped.z * 2654435761u;
    h ^= h >> 15u;
    h *= 0x2c1b3c6du;
    h ^= h >> 12u;
    h *= 0x297a2d39u;
    h ^= h >> 15u;

    return float(h >> 16u) / 65535.0;
}

/// Which channel of the tile each draw takes. A pair costs two, which is why the bounce leaves a gap.
///
/// **A channel apiece, not a salt on a shared one.** Every draw a pixel makes has to be uncorrelated
/// with every other, and the fog's march offset and the bounce's elevation were literally the same
/// number until the streams were separated — a pixel whose fog started late also bounced near its
/// normal.
const uint STREAM_FOG = 0u;
const uint STREAM_BOUNCE = 1u;

/// How far each stream's sequence advances between frames.
///
/// **An additive recurrence with an irrational step**, which is the cheapest sequence whose every
/// prefix covers `[0, 1)` evenly rather than only its powers of two. The fog draws one number and
/// takes the golden ratio; the bounce draws a pair and takes R2's steps, the plastic constant's
/// first two powers, which is the same construction in two dimensions.
///
/// A rational step would close into a cycle and the frames after it would resample what the ones
/// before had already asked.
const float STREAM_TURN[RANDOM_STREAMS] = float[](0.6180340, 0.7548777, 0.5698403);

/// One number in `[0, 1)` for `pixel`, from this frame's `stream`th draw.
///
/// **Blue noise across the screen, a low-discrepancy sequence along time.** The tile decides how a
/// pixel's draw differs from its neighbours' — deliberately, so that the error between them
/// alternates rather than clumping into blotches a filter would read as shading. The turn decides
/// how it differs from its own last frame, so the samples a pixel accumulates sweep the interval
/// instead of stumbling about in it.
///
/// Shifting every value by the same amount and wrapping is Cranley and Patterson's rotation: it
/// moves which pixel holds which number and leaves the arrangement's spectrum where it was.
float randomAt(uvec2 pixel, uint stream)
{
    const uvec2 tile = pixel % BLUE_NOISE_EXTENT;
    const uint at = (tile.y * BLUE_NOISE_EXTENT + tile.x) * RANDOM_STREAMS + stream;

    return fract(blueNoise[at] + float(frame.mFrame) * STREAM_TURN[stream]);
}

/// Two numbers in `[0, 1)` for one pixel, from `stream` and the one after it.
vec2 unitPair(uvec2 pixel, uint stream)
{
    return vec2(randomAt(pixel, stream), randomAt(pixel, stream + 1u));
}

#endif
