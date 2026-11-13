#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MIPCHAIN_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_MIPCHAIN_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// A chain of halvings under an image whose first level a dispatch wrote, in one dispatch by AMD's single-pass
// downsampler (`extern/fidelityfx/gpu/spd`): `mipchain.comp`, which `MipChainPass` records.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The most levels a chain is built down by: the downsampler's twelve, under a first level of 4096 texels a side.
    const uint MIPCHAIN_LEVELS_MOST = 12u;

    /// Where `mipchain.comp` binds what it reads and writes in set 0, and how many there are: the first level, the
    /// counter the workgroups of the chain meet at, and a binding a level written, the first of them the image's
    /// second level.
    const uint MIPCHAIN_BIND_SOURCE = 0;
    const uint MIPCHAIN_BIND_COUNTER = 1;
    const uint MIPCHAIN_BIND_LEVEL_0 = 2;
    const uint MIPCHAIN_BINDINGS = MIPCHAIN_BIND_LEVEL_0 + MIPCHAIN_LEVELS_MOST;

    /// The downsampler's workgroup: 256 lanes, a tile of 64 by 64 texels of the first level.
    const uint MIPCHAIN_WORKGROUP = 256u;
    const uint MIPCHAIN_TILE = 64u;

    /// How many chains one record builds side by side, a counter each: the sea's two cascades of two tiles.
    const uint MIPCHAIN_CHAINS_MOST = 4u;

    /// How far apart the counters stand, in bytes, each bound alone: the largest `minStorageBufferOffsetAlignment`
    /// Vulkan allows. Bound whole, the chains of one record wrote one buffer, and synchronization validation reads
    /// that as a hazard between them, which it cannot tell apart from one.
    const uint MIPCHAIN_COUNTER_STRIDE = 256u;

    /// What one chain is told.
    struct MipChainConstants
    {
        /// The first level's side, square and a power of two: each level is exactly half the one above it.
        uint mSide;

        /// How many levels are written below the first.
        uint mLevels;

        /// How many workgroups the dispatch launched, which the last of them to finish counts to.
        uint mWorkGroups;

        /// One where the image is display-encoded and its storage view is not (`ImageDescription::mStorageFormat`):
        /// each texel is decoded before the mean and the mean encoded before it is stored, so the chain averages light
        /// as a blit through the encoded format does.
        uint mEncoded;
    };

#ifdef RTX_HOST
    static_assert(sizeof(MipChainConstants) == 16, "MipChainConstants must be scalar-packed on every side");
}
#endif

#endif
