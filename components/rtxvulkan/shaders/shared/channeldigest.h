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

    /// What one dispatch is told: how far the images reach, which is one extent for all of them;
    /// which image of the frame its first float slot and its first word slot hold, counted as the
    /// lanes count them (`DIGEST_LANES` words an image); and which slots are bound, a bit each. The
    /// rest are a stand-in of one texel, which nothing reads, and digest as nothing.
    struct DigestConstants
    {
        uint mWidth;
        uint mHeight;
        uint mFloatFirst;
        uint mFloats;
        uint mWordFirst;
        uint mWords;
    };

#ifdef RTX_HOST
    static_assert(sizeof(DigestConstants) == 24, "DigestConstants must be scalar-packed on every side");
}
#endif

#endif
