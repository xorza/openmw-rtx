#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RGTC_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_RGTC_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// The BC4 and BC5 encoder's dispatch, `rgtcencode.comp`: a chain of one channel or two, each a BC4
// block, as Vulkan's `RGTC` formats hold them. What it shares with the other encoders is
// `blockencode.h`'s.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The bytes one channel's block is stored in: two end points of a byte and sixteen indices of
    /// three bits. A BC5 block is two, red's first.
    const uint RGTC_CHANNEL_BYTES = 8u;

    /// The values a block's indices name where its first end point is the larger: both end points
    /// and six between them, in sevenths.
    const uint RGTC_STEPS = 7u;

    struct RgtcConstants
    {
        /// The first level's texels across and down; each level after is half, never under one.
        uint mWidth;
        uint mHeight;

        uint mLevels;

        /// One for BC4, red; two for BC5, red and green.
        uint mChannels;
    };

#ifdef RTX_HOST
    static_assert(sizeof(RgtcConstants) == 16, "RgtcConstants must be scalar-packed on every side");
}
#endif

#endif
