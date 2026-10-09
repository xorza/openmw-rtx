#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_BC7_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_BC7_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// The BC7 encoder's dispatch: what `bc7encode.comp` is told about the level it encodes, and the one
// mode it writes.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// Where `bc7encode.comp` binds what it reads and writes in set 0, and how many there are.
    const uint BC7_BIND_SOURCE = 0;
    const uint BC7_BIND_BLOCKS = 1;
    const uint BC7_BINDINGS = 2;

    /// Lanes in one workgroup, a block each.
    const uint BC7_WORKGROUP = 64u;

    /// The levels one dispatch encodes at the most, each a storage view of its own in one array:
    /// more than a chain from the largest side a device takes holds.
    const uint BC7_MOST_LEVELS = 16u;

    /// Texels a block holds along each side, and the bytes it is stored in.
    const uint BC7_BLOCK_SIDE = 4u;
    const uint BC7_BLOCK_BYTES = 16u;

    /// Mode 6's three-bit field of the first index: its top bit is implied nought, which is why the
    /// encoder swaps a block's end points where the first texel's index would need it.
    const uint BC7_ANCHOR_BITS = 3u;
    const uint BC7_INDEX_BITS = 4u;
    const uint BC7_ENDPOINT_BITS = 7u;

    /// Mode 6's sixteen weights out of 64, which the format defines and every decoder applies as
    /// `((64 - w) * e0 + w * e1 + 32) >> 6`.
    const uint BC7_WEIGHT_TOTAL = 64u;

    /// The index a block of one colour is encoded at, whose weight, 30, lies near the middle: a
    /// pair of end points either side of each channel reaches every byte there, which two equal
    /// end points sharing one lowest bit across the channels do not.
    const uint BC7_SINGLE_INDEX = 7u;

    /// One image's chain, which is one dispatch: a lane a block, the levels' blocks one after
    /// another, each level's row by row (`Rtx::Bc7Chain`), and each level's lanes whole workgroups of
    /// their own, so a workgroup reads one level.
    struct Bc7Constants
    {
        /// The first level's texels across and down; each level after is half, never under one.
        uint mWidth;
        uint mHeight;

        uint mLevels;

        /// Whether a reader reads the alpha, so its error counts: nought for an opaque image, whose
        /// alpha the four channels' shared lowest bit may then give up for the colour's.
        uint mWeighsAlpha;
    };

#ifdef RTX_HOST
    static_assert(sizeof(Bc7Constants) == 16, "Bc7Constants must be scalar-packed on every side");
}
#endif

#endif
