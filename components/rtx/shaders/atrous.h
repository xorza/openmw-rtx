#ifndef OPENMW_COMPONENTS_RTX_SHADERS_ATROUS_H
#define OPENMW_COMPONENTS_RTX_SHADERS_ATROUS_H

#include "camera.h"
#include "hosttypes.h"
#include "look.h"
#include "portable.h"
#include "storageformat.h"

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
    const uint ATROUS_BINDINGS = 3;

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
        Camera mCamera;

        /// The eye the player's arms were traced through, `VisibilityConstants::mArms`: a pixel the
        /// trace drew on an arm — the puffs channel's flag says which — is rebuilt through it, and
        /// the rest through `mCamera`. The two stand at one place, so positions rebuilt through
        /// either still differ by a vector that drops it.
        Camera mArms;

        /// The spacing of this level's taps, in pixels. The three sigmas the taps are weighed by
        /// are `look.h`'s, because nothing varies them per level or per frame.
        uint mStep;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(AtrousConstants) == 140, "AtrousConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
