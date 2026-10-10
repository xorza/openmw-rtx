#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_PANE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_PANE_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

#include "accumulate.h"

// What the pane filter needs: a temporal accumulator over `CHANNEL_PANE`, which `pane.comp` says the
// shape of. Included verbatim by both sides, for the reason `visibility.h` is.
//
// **The mean in halves rounded at random and its count beside it**, for the reasons `specular.h`
// gives for the glossy filter's, which keeps the same history. **And the surface the history belongs
// to, of its own**, as the accumulator's surface — the layer's, which the accumulator's does not
// describe.

#define PANE_MEAN STORAGE_RGBA16F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `pane.comp` binds what it reads and writes in set 0, and how many there are.
    const uint PANE_BIND_PANE = 0;
    const uint PANE_BIND_SURFACE = 1;
    const uint PANE_BIND_MOTION = 2;
    const uint PANE_BIND_HELD_BEFORE = 3;
    const uint PANE_BIND_MEAN_BEFORE = 4;
    const uint PANE_BIND_MEAN = 5;
    const uint PANE_BIND_FAST_BEFORE = 6;
    const uint PANE_BIND_FAST_BLENDED = 7;
    const uint PANE_BINDINGS = 8;

    /// Threads along each edge of the pane filter's workgroup.
    const uint PANE_WORKGROUP = 8;

#ifdef RTX_HOST
}
#endif

#endif
