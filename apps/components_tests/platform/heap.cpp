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
        /// both glibc and Windows give a block this large memory of its own — and given back, to where it stood.
        TEST(PlatformHeapTest, aBlockHeldIsInUseAndGivenBackIsNot)
        {
            constexpr std::uint64_t sBytes = std::uint64_t{ 64 } << 20;
            constexpr std::uint64_t sPage = 4096;
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
                EXPECT_GE(during->mInUse - before->mInUse, sBytes);
                EXPECT_LE(during->mInUse - before->mInUse, sBytes + sPage);
                EXPECT_GE(during->mTaken, during->mInUse) << "more in use than taken";
            }
            const std::optional<HeapHeld> after = heapHeld();
            ASSERT_TRUE(after.has_value());
            EXPECT_EQ(after->mInUse, before->mInUse);
        }
    }
}
