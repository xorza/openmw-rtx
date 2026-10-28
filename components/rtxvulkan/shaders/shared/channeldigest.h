#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_CHANNELDIGEST_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_CHANNELDIGEST_H

#include <components/rtx/shaders/digest.h>
#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What `digest.comp` is told, the dispatch that digests the trace's channels for a read-back.
// `digest.h` says how a texel is digested.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `digest.comp` binds what it reads and writes in set 0, and how many there are. The
    /// shader's layout and the pass's own layout and writes are numbered by these and by nothing
    /// else, so the two cannot drift apart.
    const uint DIGEST_BIND_IMAGES = 0;
    const uint DIGEST_BIND_WORDS = 1;
    const uint DIGEST_BIND_LANES = 2;
    const uint DIGEST_BINDINGS = 3;

    /// One side of the square of texels a workgroup digests.
    const uint DIGEST_WORKGROUP = 16u;

    /// How many float images one dispatch binds at `DIGEST_BIND_IMAGES`, and how many word images
    /// at `DIGEST_BIND_WORDS`: **a run of each per dispatch, and as many dispatches as the longer
    /// run needs**, since a pass's set is pushed, and a device need push no more than 32
    /// descriptors a set, the census's among them, and the backend no more than 24 images
    /// (`DescriptorWrites::sMostImages`). Every image of the frame in one set is more.
    const uint DIGEST_FLOAT_SLOTS = 16u;
    const uint DIGEST_WORD_SLOTS = 8u;
    const uint DIGEST_SLOTS = DIGEST_FLOAT_SLOTS + DIGEST_WORD_SLOTS;

    /// What one dispatch is told: the frame's extent, which every image is read over; which image of
    /// the frame its first float slot and its first word slot hold, counted as the lanes count them
    /// (`DIGEST_LANES` words an image); which slots are bound, a bit each, the rest being a stand-in
    /// of one texel, which nothing reads, and digest as nothing; which float slots are stored in
    /// fields (`storedOf`), read through the mapping as every word slot is; and the frame's number,
    /// which says how its fields lie (`fieldSwapOf`).
    struct DigestConstants
    {
        uint mWidth;
        uint mHeight;
        uint mFloatFirst;
        uint mFloats;
        uint mFloatsInFields;
        uint mFrame;
        uint mWordFirst;
        uint mWords;
    };

#ifdef RTX_HOST
    static_assert(sizeof(DigestConstants) == 32, "DigestConstants must be scalar-packed on every side");
}
#endif

#endif
