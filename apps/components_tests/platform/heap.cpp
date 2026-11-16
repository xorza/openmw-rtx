#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/heap.hpp>

namespace Platform
{
    namespace
    {
        /// **A block held is in use, and given back is not**: 64 MiB taken moves what the heap holds in use by those
        /// bytes and up to a page more — the allocator's header and the page it rounds a block of its own to, as
        /// both glibc and Windows give a block this large memory of its own — and given back, to where it stood, each
        /// within what the binary's other threads allocate meanwhile. Each arena keeps no more free than it took, and
        /// all of them took no more than the heap.
        TEST(PlatformHeapTest, aBlockHeldIsInUseAndGivenBackIsNot)
        {
            constexpr std::uint64_t sBytes = std::uint64_t{ 64 } << 20;
            constexpr std::uint64_t sPage = 4096;
            // What the binary's other threads may take or give back between two readings, which are of the whole
            // process: a few kilobytes were seen, once in eight runs beside the run record's tests.
            constexpr std::uint64_t sOthers = 64 * 1024;
            const std::optional<HeapHeld> before = heapHeld();
            ASSERT_TRUE(before.has_value()) << "an allocator the tree runs on that does not say";
            {
                std::vector<std::byte> block(sBytes);
                // Written through a volatile view: an allocation nothing observes may be elided, as the standard lets
                // a compiler do with new and delete.
                volatile std::byte* const observed = block.data();
                observed[sBytes - 1] = std::byte{ 1 };
                const std::optional<HeapHeld> during = heapHeld();
                ASSERT_TRUE(during.has_value());
                EXPECT_GE(during->mInUse + sOthers - before->mInUse, sBytes);
                EXPECT_LE(during->mInUse - before->mInUse, sBytes + sPage + sOthers);
                EXPECT_GE(during->mTaken, during->mInUse) << "more in use than taken";
                std::uint64_t arenas = 0;
                for (const HeapArena& arena : during->mArenas)
                {
                    EXPECT_LE(arena.mFree, arena.mTaken) << "an arena with more free than it took";
                    arenas += arena.mTaken;
                }
                EXPECT_LE(arenas, during->mTaken) << "the arenas took more than the heap";
            }
            const std::optional<HeapHeld> after = heapHeld();
            ASSERT_TRUE(after.has_value());
            EXPECT_LE(after->mInUse, before->mInUse + sOthers);
            EXPECT_LE(before->mInUse, after->mInUse + sOthers);
        }
    }
}
