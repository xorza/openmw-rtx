#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HISTORYCLAMP_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HISTORYCLAMP_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What the glossy and the pane filters' anti-lag needs: `historyclamp.comp`, which says the shape of
// it. Included verbatim by both sides, for the reason `visibility.h` is.
//
// **A filter's fast mean in shared exponent, and in the second word whether the pixel holds a mean
// at all**, which the clamp's square reads at every neighbour: the slow means are written in place
// beside it, so a neighbour's is not read. The same width as the bounce's (`ACCUMULATE_FAST`).
//
// **The slow mean as the two filters keep it**, `SPECULAR_MEAN` and `PANE_MEAN`, which the pass
// checks are this.

#define HISTORY_CLAMP_FAST STORAGE_RG32UI
#define HISTORY_CLAMP_MEAN STORAGE_RGBA32F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `historyclamp.comp` binds what it reads and writes in set 0, and how many there are.
    const uint HISTORY_CLAMP_BIND_SAMPLED = 0;
    const uint HISTORY_CLAMP_BIND_MEAN = 1;
    const uint HISTORY_CLAMP_BIND_FAST = 2;
    const uint HISTORY_CLAMP_BIND_FAST_OUT = 3;
    const uint HISTORY_CLAMP_BINDINGS = 4;

    /// Threads along each edge of the clamp's workgroup.
    const uint HISTORY_CLAMP_WORKGROUP = 8;

    /// What the clamp is handed: the frame's extent, and whether it clamps at all
    /// (`FilterSwitches::mAntilag`), nought or one.
    struct HistoryClampConstants
    {
        uint mWidth;
        uint mHeight;
        uint mAntilag;
    };

#ifdef RTX_HOST
}
#endif

#endif
