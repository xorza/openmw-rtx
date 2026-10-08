#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPECULAR_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_SPECULAR_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>
#include <components/rtx/shaders/storageformat.h>

// What the glossy filter needs: a temporal accumulator over `CHANNEL_SPECULAR`, which
// `specular.comp` says the shape of. Included verbatim by both sides, for the reason `visibility.h`
// is.
//
// **The mean in halves, rounded at random at every store, and its frame count in the fourth
// channel.** A half store rounds toward nought on this device, so a running mean stored as it is
// falls a little at every store: measured on a metal floor, sixteen frames stood 0.13 to 0.2% under
// the average of the same frames. Rounded at random first (`roundedToHalf`), every store is exact and
// the mean's expected value is the average, at the noise of half a step. One texel, so a tap is one
// fetch.

#define SPECULAR_MEAN STORAGE_RGBA16F

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
    const uint SPECULAR_BIND_FAST_BEFORE = 6;
    const uint SPECULAR_BIND_FAST_BLENDED = 7;
    const uint SPECULAR_BINDINGS = 8;

    /// Threads along each edge of the glossy filter's workgroup.
    const uint SPECULAR_WORKGROUP = 8;

#ifdef RTX_HOST
}
#endif

#endif
