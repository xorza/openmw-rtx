#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_ATROUS_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_ATROUS_H

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What one wavelet level of the denoiser needs. Included verbatim by both sides, for the reason
// `visibility.h` is.

// What a level reads and writes, said once for both sides that have to agree.
//
// **The cascade's own, and not the trace's.** The levels run from the image the accumulator
// blended into through a pair of this pass's own, so `CHANNEL_INDIRECT` is written once
// by the trace and read once by whatever consumes it. That is what lets the two formats part: a
// reference is built through that channel and never through this one.
//
// **Halves at every level, each rounded by the shader before its store** (`lib/halfround.glsl`). A
// half store rounds toward nought on this card (`RtxHalfStoreTest`), and the first level writes the
// bounce's running mean, which the accumulator blends into the next frame (`ACCUMULATE_COLOUR` is
// this format by definition): stored as it is, it fell a step a frame, which a blend of
// `ACCUMULATE_FRAMES` keeps at up to thirty-two, 1.6 per cent under the mean of the same frames. So
// the first level rounds at random, as the accumulator and its clamp do, and the levels after it,
// whose answers are shown and never blended back, round to the nearest. **The bounce's texel holds
// the deviation in `a`, the fill's the frame count** where a history holds one, as the levels read
// them; one format at every level, so the second narrow level writes over the blend the first
// level read (`AtrousPass::record`).

#define ATROUS_CHANNEL STORAGE_RGBA16F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `atrous.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint ATROUS_BIND_SOURCE = 0;
    const uint ATROUS_BIND_FILTERED = 1;
    const uint ATROUS_BIND_SURFACE = 2;
    const uint ATROUS_BIND_FILL_SOURCE = 3;
    const uint ATROUS_BIND_FILL_FILTERED = 4;
    const uint ATROUS_BIND_FAST = 5;
    const uint ATROUS_BINDINGS = 6;

    /// Where `atrouscompose.comp` binds the composite's channels after the level's own, and how many
    /// it binds in all: the frame it composes over, the albedos, the shadowed sources and what the
    /// shadow denoiser made of them, the lobe and the layers' light with what puts each back.
    const uint ATROUS_COMPOSE_BIND_DIRECT = 6;
    const uint ATROUS_COMPOSE_BIND_ALBEDO = 7;
    const uint ATROUS_COMPOSE_BIND_AMBIENT_ALBEDO = 8;
    const uint ATROUS_COMPOSE_BIND_SHADOWED = 9;
    const uint ATROUS_COMPOSE_BIND_LAMPED = 10;
    const uint ATROUS_COMPOSE_BIND_SHADOW = 11;
    const uint ATROUS_COMPOSE_BIND_LAMP_SHADOW = 12;
    const uint ATROUS_COMPOSE_BIND_SPECULAR = 13;
    const uint ATROUS_COMPOSE_BIND_SPECULAR_ALBEDO = 14;
    const uint ATROUS_COMPOSE_BIND_PANE = 15;
    const uint ATROUS_COMPOSE_BIND_PANE_ALBEDO = 16;
    const uint ATROUS_COMPOSE_BINDINGS = 17;

    /// Where `atrous.comp`'s specialization constant sits: `ATROUS_WIDE`, true for the first level
    /// and false for every level after it.
    const uint ATROUS_SPEC_WIDE = 0u;
    const uint ATROUS_SPEC_COUNT = 1u;

    /// Threads along each edge of a level's workgroup.
    const uint ATROUS_WORKGROUP = 8;

    /// Everything one level reads that is not an image.
    ///
    /// **The camera is here because the edge tests need world positions and the surface channel
    /// stores a distance.** A position is `origin + direction * distance`, and the difference between two of
    /// them drops the origin — so the basis is enough and the eye's place in the world is not
    /// needed. The rays are rebuilt by the same `rayAt` the trace built them with, which is what
    /// makes the reconstructed positions the ones that were actually shaded.
    struct AtrousConstants
    {
        /// The eyes the frame was traced through, `VisibilityConstants::mEyes`. The two stand at one
        /// place, so positions rebuilt through either still differ by a vector that drops it.
        Eyes mEyes;

        /// The spacing of a narrow level's taps, in pixels; the first level steps one, which its tile
        /// in shared memory holds, whatever this says. The three sigmas the taps are weighed by are
        /// `look.h`'s, because nothing varies them per level or per frame.
        uint mStep;

        /// The longest history this level rebuilds from the surface around it rather than filters
        /// (`ACCUMULATE_FIX_FRAMES`): the first level's, where the run asks for the history fix, and
        /// nought at every other level and in a picture, which has no settled neighbour to borrow
        /// from. A narrow level reads none.
        float mFixFrames;

        /// The frame's number, `VisibilityConstants::mFrame`, which a level whose step passes
        /// `ATROUS_JITTER_STEP` draws its taps' offset by.
        uint mFrame;

        /// What the composing level composes by, `CompositeConstants::mShadowed` and `mLobed`, and
        /// what no other level reads.
        uint mShadowed RTX_ZERO;
        uint mLobed RTX_ZERO;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(AtrousConstants) == 172, "AtrousConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
