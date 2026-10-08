#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_CHANNELDIGEST_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_CHANNELDIGEST_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What `digest.comp` is told, the dispatch that digests the trace's channels for a read-back.
// `digest.h` says how a texel is digested.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// What the dispatch is told: how far the images reach, which is one extent for all of them.
    struct DigestConstants
    {
        uint mWidth;
        uint mHeight;
    };

#ifdef RTX_HOST
    static_assert(sizeof(DigestConstants) == 8, "DigestConstants must be scalar-packed on every side");
}
#endif

#endif
