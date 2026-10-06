#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLUENOISE_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_BLUENOISE_GLSL

// The blue-noise tile's draws, turned frame by frame: what the trace's `randomAt` reads and the tone
// pass's dither, which has no frame block to read the tile through and is handed it instead.

#include "scene.h"

/// How far each stream's sequence advances between frames.
///
/// **An additive recurrence with an irrational step**, which is the cheapest sequence whose every
/// prefix covers `[0, 1)` evenly rather than only its powers of two. The fog draws one number and
/// takes the golden ratio; a pair takes R2's steps, the plastic constant's first two powers, which
/// is the same construction in two dimensions.
///
/// A rational step would close into a cycle and the frames after it would resample what the ones
/// before had already asked.
///
/// **The one part of the stream table that stays here**, because a constant array is spelled
/// `float[](...)` in GLSL and `{...}` in C++ and there is no third spelling both compile. It is
/// `RANDOM_STREAMS` long by declaration, so the count still binds it; what a second shader needs to
/// know — which channels are taken — is the `STREAM_` ids, and those sit with the count in
/// `scene.h`.
///
/// The two-dimensional `R2` pair for the fog's column and for the bounce, `sqrt(2) - 1` for the
/// water and the golden ratio for the fog's march — two different irrationals for the two single
/// numbers, because two streams turning by the same step differ only by where they started and
/// converge on the same sweep. The two pairs share `R2`, and are never read together.
///
/// **The split hit's four draws are read together**, so each turns by its own: the sun's pair by
/// `R2`, which nothing beside it reads, the lamp's by `(sqrt 3 - 1, sqrt 7 - 2)`, and the two picks
/// by `sqrt 11 - 3` and `sqrt 13 - 3`. Square roots of distinct square-free numbers are independent
/// over the rationals, and of the plastic constant too, so no two of these turn in step. The tone
/// pass's dither turns by `sqrt 17 - 4`, for the same reason.
const float STREAM_TURN[RANDOM_STREAMS] = float[](0.7548777, 0.5698403, 0.7548777, 0.5698403, 0.4142136, 0.6180340,
    0.7548777, 0.5698403, 0.7320508, 0.6457513, 0.3166248, 0.6055513, 0.1231056);

/// Where `pixel`'s draw of `stream` stands in the tile, `RANDOM_STREAMS` channels a texel.
uint tileIndex(uvec2 pixel, uint stream)
{
    const uvec2 tile = pixel % BLUE_NOISE_EXTENT;
    return (tile.y * BLUE_NOISE_EXTENT + tile.x) * RANDOM_STREAMS + stream;
}

/// A tile's value of `stream`, turned for frame `index`.
///
/// **The turn in fixed point, because a float product keeps no fraction of a long session.**
/// `float(frame) * step` rounds to the product's own last place, which past 2^17 frames — half an
/// hour at sixty — is a 128th of the interval, past 2^20 a sixteenth, and past 2^23 two values: the
/// sweep over frames freezes into a few offsets. The step's float times 2^32 is a whole number, so
/// the product wrapped in a word is the fraction of `frame * step` exactly, at any count.
float turnedTile(float tiled, uint index, uint stream)
{
    const uint turned = index * uint(STREAM_TURN[stream] * 4294967296.0);
    return fract(tiled + float(turned >> 8u) * (1.0 / 16777216.0));
}

#endif
