#include <cstdint>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/heapdescription.hpp>
#include <components/platform/heap.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;

        /// **The arenas are said where the allocator keeps them apart, the most free first and a tie in their order**:
        /// none says the heap alone, 300 taken and 250 in use, 50 free; two keeping 5 and 5 of 20 and 30 say both, the
        /// first numbered first; one is said alone, as one arena.
        TEST(RtxHeapDescriptionTest, theArenasAreSaidMostFreeFirstAndATieInTheirOrder)
        {
            Platform::HeapHeld heap{ .mTaken = 300 * sMiB, .mInUse = 250 * sMiB, .mArenas = {} };
            EXPECT_EQ(describeHeap(heap), "the heap took 300.0 MiB, 50.0 MiB of it free");

            heap.mArenas = { { .mTaken = 20 * sMiB, .mFree = 5 * sMiB }, { .mTaken = 30 * sMiB, .mFree = 5 * sMiB } };
            EXPECT_EQ(describeHeap(heap),
                "the heap took 300.0 MiB, 50.0 MiB of it free, in 2 arenas, the most free 5.0 of 20.0 MiB, 5.0 of 30.0 "
                "MiB");

            heap.mArenas = { { .mTaken = 300 * sMiB, .mFree = 50 * sMiB } };
            EXPECT_EQ(describeHeap(heap),
                "the heap took 300.0 MiB, 50.0 MiB of it free, in 1 arena, the most free 50.0 of 300.0 MiB");
        }
    }
}
