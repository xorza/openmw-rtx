#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPECULAR_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPECULAR_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What the glossy filter needs: a temporal accumulator over `CHANNEL_SPECULAR`, which
// `specular.comp` says the shape of. Included verbatim by both sides, for the reason `visibility.h`
// is.
//
// **The mean in full floats, and its frame count in the fourth channel.** Halves round toward
// nought where this device stores them, so a running mean kept in halves falls a little at every
// store: measured on a metal floor, sixteen frames stood 0.13 to 0.2% under the average of the same
// frames, where full floats keep the average to its rounding. One texel, so a tap is one fetch.

#define SPECULAR_MEAN STORAGE_RGBA32F

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `specular.comp` binds what it reads and writes in set 0, and how many there are.
    const uint SPECULAR_BIND_SPECULAR = 0;
    const uint SPECULAR_BIND_SURFACE = 1;
    const uint SPECULAR_BIND_MOTION = 2;
    const uint SPECULAR_BIND_HELD_SURFACE = 3;
    const uint SPECULAR_BIND_MEAN_BEFORE = 4;
    const uint SPECULAR_BIND_MEAN = 5;
    const uint SPECULAR_BINDINGS = 6;

    /// Threads along each edge of the glossy filter's workgroup.
    const uint SPECULAR_WORKGROUP = 8;

#ifdef RTX_HOST
}
#endif

#endif
