#ifndef OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TOPLEVELPACK_H
#define OPENMW_COMPONENTS_RTXVULKAN_SHADERS_SHARED_TOPLEVELPACK_H

#include <components/rtx/shaders/hosttypes.h>
#include <components/rtx/shaders/portable.h>

// `<cstddef>` for the `offsetof` the pinned layout below is checked with, last because only the
// host has it.
#ifdef RTX_HOST
#include <cstddef>
#endif

// What `toplevelpack.comp` is handed: the top level's rows, which hold a row for every instance slot
// the scene ever handed out, packed into the rows a build reads, which hold the placed ones alone.

#ifdef RTX_HOST
namespace Rtx::Shaders
{
#endif

    /// The rows a workgroup packs, and so how many rows each of the starts the host hands over
    /// covers: a block.
    const uint TOP_LEVEL_PACK_WORKGROUP = 256u;

    /// The sixteen-byte words in a row, `VkAccelerationStructureInstanceKHR`, which the pass moves
    /// whole, and the word whose last two halves are the structure's reference — nought in a row that
    /// places nothing.
    const uint TOP_LEVEL_ROW_WORDS = 4u;
    const uint TOP_LEVEL_REFERENCE_WORD = 3u;

    struct TopLevelPackConstants
    {
        /// The rows by slot, and where they go: one a row placed, in slot order.
        uint64 mRows;
        uint64 mPacked;

        /// For each block of `TOP_LEVEL_PACK_WORKGROUP` rows, how many rows the blocks before it
        /// place: where its first placed row is packed.
        uint64 mStarts;

        /// The rows by slot.
        uint mCount;
    };

#ifdef RTX_HOST
    static_assert(offsetof(TopLevelPackConstants, mCount) + sizeof(uint) == 28,
        "TopLevelPackConstants must be scalar-packed on every side");
    static_assert(TOP_LEVEL_PACK_WORKGROUP % 32u == 0u, "a block of rows past a whole number of words");
}
#endif

#endif
