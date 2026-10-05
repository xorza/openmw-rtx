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
// **The cascade's own, and not the trace's.** The levels ping-pong between the image the
// accumulator blended into and a scratch of this pass's own, so `CHANNEL_INDIRECT` is written once
// by the trace and read once by whatever consumes it. That is what lets the two formats part: a
// reference is built through that channel and never through this one.
//
// **Full floats, because the first level writes the bounce's running mean**, which the accumulator
// blends into the next frame (`ACCUMULATE_COLOUR` is this format by definition). A half store
// rounds toward nought on this card (`RtxHalfStoreTest`), so a mean kept in halves falls a little
// at every store: up to one step a frame, which the blend's weight keeps at up to sixteen, about
// 0.8 per cent under the mean of the same frames. One declaration writes every level, so every
// level is full float; the levels after the first are shown and never summed, and would keep halves
// on their own.

#define ATROUS_CHANNEL STORAGE_RGBA32F

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
    const uint ATROUS_BIND_MOMENTS = 5;
    const uint ATROUS_BINDINGS = 6;

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

        /// The spacing of this level's taps, in pixels. The three sigmas the taps are weighed by
        /// are `look.h`'s, because nothing varies them per level or per frame.
        uint mStep;

        /// The longest history this level rebuilds from the surface around it rather than filters
        /// (`ACCUMULATE_FIX_FRAMES`): the first level's, where the run asks for the history fix, and
        /// nought at every other level and in a picture, which has no settled neighbour to borrow
        /// from. A narrow level reads none.
        float mFixFrames;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(AtrousConstants) == 144, "AtrousConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
