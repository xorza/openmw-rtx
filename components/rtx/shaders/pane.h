#ifndef OPENMW_COMPONENTS_RTX_SHADERS_PANE_H
#define OPENMW_COMPONENTS_RTX_SHADERS_PANE_H

#include "camera.h"
#include "hosttypes.h"
#include "portable.h"
#include "storageformat.h"

// What the pane filter needs: a temporal accumulator over `CHANNEL_PANE`, which `pane.comp` says the
// shape of. Included verbatim by both sides, for the reason `visibility.h` is.
//
// **The mean in full floats and its count beside it**, for the reasons `specular.h` gives for the
// glossy filter's, which keeps the same history. **And the surface the history belongs to, of its
// own**, as the accumulator's surface — the layer's, which the accumulator's does not describe.

#define PANE_MEAN STORAGE_RGBA32F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `pane.comp` binds what it reads and writes in set 0, and how many there are.
    const uint PANE_BIND_PANE = 0;
    const uint PANE_BIND_SURFACE = 1;
    const uint PANE_BIND_MOTION = 2;
    const uint PANE_BIND_HELD_BEFORE = 3;
    const uint PANE_BIND_HELD = 4;
    const uint PANE_BIND_MEAN_BEFORE = 5;
    const uint PANE_BIND_MEAN = 6;
    const uint PANE_BINDINGS = 7;

    /// Threads along each edge of the pane filter's workgroup.
    const uint PANE_WORKGROUP = 8;

    /// What the pane filter reads that is not an image.
    struct PaneConstants
    {
        /// The eye, for its extent and the jitter the layers' motion was written against. The
        /// arms' eye is jittered alike, so one serves both.
        Camera mCamera;

        /// Non-zero where there is no history to reuse, as the accumulator is told.
        uint mReset;

        /// What a world distance is multiplied by before the history holds it —
        /// `AccumulateConstants::mDistanceScale`, the same rule over the layers' distances.
        float mDistanceScale;
    };

    // Pinned for the reason `scene.h` gives: the side that writes these bytes and the side that
    // reads them are different compilers.
#ifdef RTX_HOST
    static_assert(sizeof(PaneConstants) == 68, "PaneConstants must be scalar-packed on every side");
#endif

#ifdef RTX_HOST
}
#endif

#endif
