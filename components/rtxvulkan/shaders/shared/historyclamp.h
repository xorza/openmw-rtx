#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HISTORYCLAMP_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_HISTORYCLAMP_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What the glossy and the pane filters' anti-lag needs: `historyclamp.comp`, which says the shape of
// it. Included verbatim by both sides, for the reason `visibility.h` is.
//
// **A filter's fast mean in one shared-exponent word, which also says whether the pixel holds a
// mean at all** (`HISTORY_CLAMP_EMPTY`), as the clamp's square reads it at every neighbour: the slow
// means are written in place beside it, so a neighbour's is not read.
//
// **The slow mean as the two filters keep it**, `SPECULAR_MEAN` and `PANE_MEAN`, which the pass
// checks are this.

#define HISTORY_CLAMP_FAST STORAGE_R32UI
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

    /// The fast mean of a pixel that holds none: an exponent field of one over three mantissas of
    /// nought, which `unpackRgb9e5` reads as nought. **A word `packRgb9e5` never writes**: a field of
    /// one is an exponent of -14 its floor did not raise, so the brightest channel stands in
    /// `[2^-15, 2^-14)` and its mantissa rounds to at least 256.
    const uint HISTORY_CLAMP_EMPTY = 1u << 27u;

    /// What the clamp is handed: the frame's extent, and whether it clamps at all
    /// (`FilterSwitches::mAntilag`), nought or one.
    struct HistoryClampConstants
    {
        uint mWidth;
        uint mHeight;
        uint mAntilag;
    };

#ifdef RTX_HOST
    static_assert(sizeof(HistoryClampConstants) == 12, "HistoryClampConstants must be scalar-packed on every side");
}
#endif

#endif
