#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADOW_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SHADOW_H

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

#include "accumulate.h"

// What the shadow denoiser needs: a port of AMD's FidelityFX Shadow Denoiser (SDK v1.1.4), over the
// one bit a field's channel holds per pixel, `CHANNEL_SHADOWED`'s or `CHANNEL_LAMPED`'s. Included verbatim by both
// sides, for the reason `visibility.h` is. `shadowtiles.comp` and `shadowfilter.comp` each say what their half of the
// port keeps and what it changes.

// What the passes keep, said once for both sides that have to agree.
//
// **Full floats where the SDK keeps halves.** The temporal pass's answer and the filter's levels are
// a mean and a variance, which the SDK stores in two halves, and the moments — a mean, a running
// sum of squared deviations and a count — which it stores in `R11G11B10_FLOAT`. Both are read back
// into their own blend: the moments by the next frame's temporal pass, and the first level's answer
// as the history it blends into. A half store rounds toward nought on this card (`RtxHalfStoreTest`),
// so a history kept in halves fell a little at every store, and a penumbra stood 0.68% dark after
// 256 frames (`RtxPenumbraDenoiseTest`). The levels after the first are scratch and could be
// halves, but one pipeline writes all three, and a layout is the pipeline's.
//
// **The rays' bits, packed**: two words an 8×4 tile of pixels, bit `(y % 4) * 8 + x % 8` of each
// for that pixel — the SDK's layout. The first is one where the pixel receives and its rays got
// through, and the second one where it receives at all (`receivesShadowed`). The classification
// reads the eighteen texels around its tile rather than the 576 pixels they stand for.
//
// **The second word is not the SDK's**, which has the first alone. A pixel the shadowed sources do
// not light keeps a bit of one, since nothing was split off it, and the classification's local
// mean counted it as lit: a receiver in a hard shadow beside a face turned from the lamp was held
// up to that mean, and showed a seam of lamp light the shadow should have hidden. At the mages'
// guild's planter one such pixel stood at 76 of 255 where 256 raw frames hold 3.
//
// **Two halves a tile for the classification**: one where the tile was cleared — every receiver in
// it and around it lit alike, so its value is exact and nothing filters it — and nought where the
// filter runs; and the tile's reach, the widest penumbra in pixels in it and the tiles around it,
// which a filter level's step must not pass (`shadowfilter.comp`). The value a cleared tile stands
// at is the temporal pass's own answer, which it wrote as exactly nought or one.
//
// **And a half a tile for the penumbra**: the widest the mask pass found in the tile
// (`CHANNEL_PENUMBRA`), which the temporal pass widens to the tiles around it.

#define SHADOW_MASK STORAGE_RG32UI
#define SHADOW_REPROJECTED STORAGE_RG32F
#define SHADOW_MOMENTS STORAGE_RGBA32F
#define SHADOW_TILES STORAGE_RG16F
#define SHADOW_PENUMBRA_TILES STORAGE_R16F

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

    /// How far either way of a pixel the SDK's local neighbourhood kernel reaches — its radius, and
    /// the apron the classification reads past its tile on every side, so a square of
    /// `SHADOW_WORKGROUP + 2 * SHADOW_REACH` bits: three texels of the mask across and six down.
    /// Held whole in a word a row and started on a tile's corner, which the asserts below say of
    /// it. The three filter levels reach one, two and four pixels, seven in all, inside it.
    const uint SHADOW_REACH = 8;

    /// Where `shadowmask.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_MASK_BIND_SHADOWED = 0;
    const uint SHADOW_MASK_BIND_SURFACE = 1;
    const uint SHADOW_MASK_BIND_MASK = 2;
    const uint SHADOW_MASK_BIND_PENUMBRA = 3;
    const uint SHADOW_MASK_BIND_PENUMBRA_TILES = 4;
    const uint SHADOW_MASK_BINDINGS = 5;

    /// The three levels the spatial filter runs at: the level is the filter module's one
    /// specialization constant, and its taps stand `1 << level` pixels apart.
    const uint SHADOW_FILTER_LEVELS = 3;

    /// The filter module's specialization constants, by `constant_id`: the level alone.
    const uint SHADOW_SPEC_LEVEL = 0u;
    const uint SHADOW_SPEC_COUNT = 1u;

    /// Where `shadowtiles.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_TILES_BIND_SURFACE = 0;
    const uint SHADOW_TILES_BIND_MOTION = 1;
    const uint SHADOW_TILES_BIND_HELD_SURFACE = 2;
    const uint SHADOW_TILES_BIND_HISTORY = 3;
    const uint SHADOW_TILES_BIND_MOMENTS_BEFORE = 4;
    const uint SHADOW_TILES_BIND_MOMENTS = 5;
    const uint SHADOW_TILES_BIND_REPROJECTED = 6;
    const uint SHADOW_TILES_BIND_TILES = 7;
    const uint SHADOW_TILES_BIND_MASK = 8;
    const uint SHADOW_TILES_BIND_PENUMBRA_TILES = 9;
    const uint SHADOW_TILES_BINDINGS = 10;

    /// Where `shadowfilter.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SHADOW_FILTER_BIND_SURFACE = 0;
    const uint SHADOW_FILTER_BIND_TILES = 1;
    const uint SHADOW_FILTER_BIND_SOURCE = 2;
    const uint SHADOW_FILTER_BIND_FILTERED = 3;
    const uint SHADOW_FILTER_BINDINGS = 4;

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

    /// What a filter level reads that is not an image: the two eyes a pixel's ray can have left, so
    /// its position is rebuilt through the one that cast it, as the wavelet rebuilds it.
    struct ShadowFilterConstants
    {
        Eyes mEyes;
    };

    /// Whether a field's source lights a pixel at all: a surface stands there, `normalCode` its
    /// `CHANNEL_SURFACE` code, and the light it would add unshadowed, the field channel's, has a
    /// luminance `unshadowed` over nought. **The one rule for who receives**, which the mask pass
    /// packs and every later pass reads from it.
    RTX_SHADER bool receivesShadowed(float normalCode, float unshadowed)
    {
        return normalCode != SURFACE_NO_NORMAL && unshadowed > 0.0f;
    }

    /// A word with its `count` low bits set, for `count` from one to the whole word.
    ///
    /// **A full word shifted right, and not one shifted left less one**: `(1u << count) - 1u` shifts
    /// by 32 for a full word, which both languages leave undefined, and a classification row as wide
    /// as its word is a row `SHADOW_REACH` allows. Nought is the one count it does not take, and a row
    /// holds at least the workgroup's own pixels.
    RTX_SHADER uint lowBits(uint count)
    {
        return ~0u >> (32u - count);
    }

#ifdef RTX_HOST
    static_assert(SHADOW_WORKGROUP + 2 * SHADOW_REACH <= 32, "a row of the classification's square past a word");
    static_assert(SHADOW_REACH == SHADOW_WORKGROUP,
        "the temporal pass reads the penumbra of the eight tiles around its own as the apron's");
    static_assert(SHADOW_MASK_WIDTH == SHADOW_WORKGROUP && 2 * SHADOW_MASK_HEIGHT == SHADOW_WORKGROUP,
        "the mask pass's workgroup is one tile of the classification, whose penumbra it writes");
    static_assert(SHADOW_REACH % SHADOW_MASK_WIDTH == 0 && SHADOW_WORKGROUP % SHADOW_MASK_WIDTH == 0,
        "the classification's square not on whole tiles");
#endif

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(ShadowMaskConstants) == 8, "ShadowMaskConstants must be scalar-packed on every side");
    static_assert(sizeof(ShadowFilterConstants) == 152, "ShadowFilterConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
