#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PIXELS_GLSL
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_LIB_PIXELS_GLSL

// The one test every dispatch in this renderer opens with, and where a shown pixel lands on the
// traced grid.
//
// **Nothing is bound here on purpose.** A standalone pass reaches this file without reaching the
// descriptor set, which is what lets `ripplecompose.comp` and `histogram.comp` read it beside
// `fogintegrate.comp`.

#include "camera.h"
#include "gbuffer.h"
#include "shared/fields.h"

/// Whether this invocation fell off the edge of what the dispatch covers.
///
/// **One spelling, because twelve passes had four.** A workgroup covers the picture in whole
/// groups, so the last group along each axis runs threads the picture has no pixel for. Each pass
/// said so its own way — an `ivec2` against two casts, a `uvec2` against two fields, an `any` over
/// a `greaterThanEqual` — and a reader had to check that the fourth form meant the first.
bool outsideOf(uvec2 pixel, uvec2 extent)
{
    return any(greaterThanEqual(pixel, extent));
}

/// The same for a tap that may have stepped off the near edge as well as the far one.
bool outsideOf(ivec2 pixel, uvec2 extent)
{
    return any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(uvec2(pixel), extent));
}

/// Whether a pixel's field follows one water path of two, out of its `CHANNEL_MOTION`, whose fourth
/// is one there and nought elsewhere: the pixels the composite blends with the four neighbours across
/// them that split as well, the layers' stage the lift of, and whose history a temporal filter reads
/// in their own field (`HistoryFields`).
bool splitIn(vec4 motion)
{
    return motion.w != 0.0;
}

/// The neighbour across `pixel` on `side` of the four: left, right, above and below.
ivec2 acrossOf(ivec2 pixel, int side)
{
    return pixel + ivec2(side == 0 ? -1 : (side == 1 ? 1 : 0), side == 2 ? -1 : (side == 3 ? 1 : 0));
}

/// Where an image stored in fields keeps `pixel` of a frame `width` wide: in its checkerboard field's
/// half (`storedOf`), under the `swap` of the frame that wrote it (`fieldSwapOf`): this frame's for
/// what it wrote, the previous frame's for a history. A tap off the frame's edge has no place, and is
/// tested with `outsideOf` first.
ivec2 storedAt(ivec2 pixel, uint width, uint swap)
{
    return ivec2(storedOf(uvec2(pixel), swap, width));
}

/// Where `channel` keeps `pixel` of a frame `width` wide, traced under `swap`: in its field where the
/// channel is stored in fields (`channelInFields`), and at the pixel itself where it is not.
ivec2 channelAt(uint channel, ivec2 pixel, uint width, uint swap)
{
    return channelInFields(channel) ? storedAt(pixel, width, swap) : pixel;
}

/// `tracedPixelUnder` along both axes.
uvec2 tracedPixelUnder(uvec2 pixel, uvec2 extent, uvec2 tracedExtent)
{
    return uvec2(
        tracedPixelUnder(pixel.x, extent.x, tracedExtent.x), tracedPixelUnder(pixel.y, extent.y, tracedExtent.y));
}

/// `shownPixelsFrom` along both axes.
uvec2 shownPixelsFrom(uvec2 traced, uvec2 extent, uvec2 tracedExtent)
{
    return uvec2(
        shownPixelsFrom(traced.x, extent.x, tracedExtent.x), shownPixelsFrom(traced.y, extent.y, tracedExtent.y));
}

/// The 3×3 tent's weight `offset` from its middle: `[1 2 1]` against itself, over sixteen, so `1/4`
/// at the middle, `1/8` along the axes and `1/16` at the corners. A select and not a table, for the
/// reason `atrous.comp`'s `kernelAt` gives.
float tentWeight(ivec2 offset)
{
    return (offset.x == 0 ? 0.5 : 0.25) * (offset.y == 0 ? 0.5 : 0.25);
}

#endif
