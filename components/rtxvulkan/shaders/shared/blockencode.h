#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_BLOCKENCODE_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_BLOCKENCODE_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// What every block encoder shares: `bc7encode.comp` and `rgtcencode.comp` read one image's chain and
// write its blocks, a lane a block, every level in one dispatch (`Rtx::BlockChain`).

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where an encoder binds what it reads and writes in set 0, and how many there are.
    const uint BLOCK_ENCODE_BIND_SOURCE = 0;
    const uint BLOCK_ENCODE_BIND_BLOCKS = 1;
    const uint BLOCK_ENCODE_BINDINGS = 2;

    /// Lanes in one workgroup, a block each.
    const uint BLOCK_ENCODE_WORKGROUP = 64u;

    /// The levels one dispatch encodes at the most, each a storage view of its own in one array:
    /// more than a chain from the largest side a device takes holds.
    const uint BLOCK_ENCODE_MOST_LEVELS = 16u;

    /// Texels a block holds along each side, in every format an encoder writes.
    const uint BLOCK_SIDE = 4u;

#ifdef RTX_HOST
}
#endif

#endif
