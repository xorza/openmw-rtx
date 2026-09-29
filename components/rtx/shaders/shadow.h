#ifndef OPENMW_COMPONENTS_RTX_SHADERS_SHADOW_H
#define OPENMW_COMPONENTS_RTX_SHADERS_SHADOW_H

#include "camera.h"
#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// What the shadow denoiser needs: a port of AMD's FidelityFX Shadow Denoiser (SDK v1.1.4), over the
// one bit `CHANNEL_SUNLIT` holds per pixel. Included verbatim by both sides, for the reason
// `visibility.h` is. `shadowtiles.comp` and `shadowfilter.comp` each say what their half of the port
// keeps and what it changes.

// What the passes keep, said once for both sides that have to agree.
//
// **The SDK's widths, but for the moments.** The temporal pass's answer and the filter's levels are
// a mean and a variance in two halves, as the SDK stores them. The moments — a mean, a running sum
// of squared deviations and a count — are `R11G11B10_FLOAT` there, a format no layout here names;
// four halves hold the same three numbers with a sign and five more bits apiece, and a pixel pays
// eight bytes for it where it paid four.
//
// **The rays' bits, packed**: one word an 8×4 tile of pixels, bit `(y % 4) * 8 + x % 8` of it one
// where that pixel's rays got through — the SDK's layout. The classification reads the eighteen
// words around its tile rather than the 576 texels they stand for.
//
// **One byte a tile for the classification**: one where the tile was cleared — every receiver in
// it and around it lit alike, so its value is exact and nothing filters it — and nought where the
// filter runs. The value a cleared tile stands at is the temporal pass's own answer, which it wrote
// as exactly nought or one.

#define SHADOW_MASK STORAGE_R32UI
#define SHADOW_REPROJECTED STORAGE_RG16F
#define SHADOW_MOMENTS STORAGE_RGBA16F
#define SHADOW_TILES STORAGE_R8

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The side of the square of pixels one workgroup of either pass covers, and so of a tile of the
    /// classification: the SDK's 8×8.
    const uint SHADOW_WORKGROUP = 8;

    /// The pixels one word of the mask packs: the SDK's 8×4 tile, so a row of the tile is a byte.
    const uint SHADOW_MASK_WIDTH = 8;
    const uint SHADOW_MASK_HEIGHT = 4;

    /// How far either way of its tile the classification reads the bits, which is how far the
    /// three filter levels reach — one, two and four pixels, seven in all — and the eight either way
    /// of a pixel the SDK's local neighbourhood kernel spans. A square of `SHADOW_WORKGROUP + 2 *
    /// SHADOW_APRON` bits: three words across and six down.
    const uint SHADOW_APRON = 8;

    /// Where `shadowmask.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_MASK_BIND_SUNLIT = 0;
    const uint SHADOW_MASK_BIND_MASK = 1;
    const uint SHADOW_MASK_BINDINGS = 2;

    /// The three levels the spatial filter runs at: the level is the filter module's one
    /// specialization constant, and its taps stand `1 << level` pixels apart.
    const uint SHADOW_FILTER_LEVELS = 3;

    /// Where `shadowtiles.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_TILES_BIND_SUNLIT = 0;
    const uint SHADOW_TILES_BIND_SURFACE = 1;
    const uint SHADOW_TILES_BIND_MOTION = 2;
    const uint SHADOW_TILES_BIND_HELD_SURFACE = 3;
    const uint SHADOW_TILES_BIND_HISTORY = 4;
    const uint SHADOW_TILES_BIND_MOMENTS_BEFORE = 5;
    const uint SHADOW_TILES_BIND_MOMENTS = 6;
    const uint SHADOW_TILES_BIND_REPROJECTED = 7;
    const uint SHADOW_TILES_BIND_TILES = 8;
    const uint SHADOW_TILES_BIND_MASK = 9;
    const uint SHADOW_TILES_BINDINGS = 10;

    /// Where `shadowfilter.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_FILTER_BIND_SURFACE = 0;
    const uint SHADOW_FILTER_BIND_PUFFS = 1;
    const uint SHADOW_FILTER_BIND_TILES = 2;
    const uint SHADOW_FILTER_BIND_SOURCE = 3;
    const uint SHADOW_FILTER_BIND_FILTERED = 4;
    const uint SHADOW_FILTER_BINDINGS = 5;

    /// The variance a pixel that receives nothing is written with, by the temporal pass and by every
    /// level after it: below every variance there is, so the levels know such a pixel from its own
    /// value and read nothing else to ask.
    const float SHADOW_NO_RECEIVER = -1.0f;

    /// What the mask pass reads that is not an image: the frame's extent, past which a bit is nought.
    struct ShadowMaskConstants
    {
        uint mWidth;
        uint mHeight;
    };

    /// What the temporal pass reads that is not an image.
    struct ShadowTilesConstants
    {
        /// The camera the frame was traced with: its extent, and the jitter the motion vector was
        /// written against, which the history's footprint adds back (`historyFootprint`).
        Camera mCamera;

        /// Non-zero where there is no history to reuse: the SDK's `IsFirstFrame`, and the
        /// accumulator's reset, whose surface history this pass reads.
        uint mReset;

        /// What the accumulator's surface history scaled a distance by —
        /// `AccumulateConstants::mDistanceScale`, the same number, because it is the same history.
        float mDistanceScale;
    };

    /// What a filter level reads that is not an image: the two eyes a pixel's ray can have left, so
    /// its position is rebuilt through the one that cast it, as the wavelet rebuilds it.
    struct ShadowFilterConstants
    {
        Camera mCamera;
        Camera mArms;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(ShadowMaskConstants) == 8, "ShadowMaskConstants must be scalar-packed on every side");
    static_assert(sizeof(ShadowTilesConstants) == 68, "ShadowTilesConstants must be scalar-packed on every side");
    static_assert(sizeof(ShadowFilterConstants) == 120, "ShadowFilterConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
