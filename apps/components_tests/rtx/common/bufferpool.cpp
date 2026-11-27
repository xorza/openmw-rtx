#include <cstddef>
#include <optional>
#include <utility>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/bufferpool.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::size_t sKiB = 1024;

        /// A buffer of `bytes` spent from a credit of as many, which the test holds the pool to have room for.
        PooledBuffer bufferOf(BufferPool& pool, std::size_t bytes)
        {
            std::optional<PoolCredit> credit = pool.reserve(bytes);
            EXPECT_TRUE(credit.has_value());
            return std::move(*credit).spend(bytes);
        }

        /// **A buffer is made in its class, and a buffer given back is the next spend's**: 100 KiB is a 128 KiB class,
        /// 10 bytes the smallest, 64 KiB, and 64 KiB itself; a buffer given back is held free, and the next spend of
        /// its class is that very buffer, so nothing more is made.
        TEST(RtxBufferPoolTest, aBufferIsMadeInItsClassAndABufferGivenBackIsTheNextSpends)
        {
            EXPECT_EQ(BufferPool::classBytesOf(100 * sKiB), 128 * sKiB);
            EXPECT_EQ(BufferPool::classBytesOf(10), 64 * sKiB);
            EXPECT_EQ(BufferPool::classBytesOf(64 * sKiB), 64 * sKiB);

            BufferPool pool(1024 * sKiB);
            const std::byte* first = nullptr;
            {
                PooledBuffer buffer = bufferOf(pool, 100 * sKiB);
                EXPECT_EQ(buffer.bytes().size(), 100 * sKiB);
                first = buffer.bytes().data();
                EXPECT_EQ(pool.getHeld().mCharged, 128 * sKiB);
                EXPECT_EQ(pool.getHeld().mFree, 0u);
            }
            EXPECT_EQ(pool.getHeld().mCharged, 0u);
            EXPECT_EQ(pool.getHeld().mFree, 128 * sKiB);

            PooledBuffer again = bufferOf(pool, 90 * sKiB);
            EXPECT_EQ(again.bytes().data(), first) << "the buffer given back";
            EXPECT_EQ(again.bytes().size(), 90 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 0u);
            again.truncate(1);
            EXPECT_EQ(again.bytes().size(), 1u);
            again = PooledBuffer();

            // **A hand-over allocates nothing once its class is warm**: a reserve, its spending and its giving back,
            // the class warmed first.
            EXPECT_EQ(Testing::allocationsIn([&] {
                for (std::size_t round = 0; round < 4; ++round)
                    std::ignore = bufferOf(pool, 100 * sKiB);
            }),
                0u);
        }

        /// **What a pool charges stays under its most, and a reserve past it is refused and never waited on**: in a
        /// pool of 256 KiB, a buffer of a 256 KiB class charged while a 128 KiB one is free drops the free one; a
        /// reserve of 64 KiB beside it is refused, and granted once it goes back. One larger than the most is granted
        /// where nothing else is charged, and refused beside 64 KiB; given back, it is kept, never freed on the giving
        /// thread, and dropped by the next spend, on the spender's.
        TEST(RtxBufferPoolTest, whatAPoolChargesStaysUnderItsMostAndAReservePastItIsRefused)
        {
            BufferPool pool(256 * sKiB);
            std::ignore = bufferOf(pool, 128 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 128 * sKiB);

            PooledBuffer whole = bufferOf(pool, 256 * sKiB);
            EXPECT_EQ(pool.getHeld().mCharged, 256 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 0u) << "the free buffer of another class dropped for room";
            EXPECT_FALSE(pool.reserve(64 * sKiB).has_value()) << "a reserve past the most";
            whole = PooledBuffer();
            EXPECT_TRUE(pool.reserve(64 * sKiB).has_value()) << "the buffer given back made room";

            {
                const std::optional<PoolCredit> small = pool.reserve(64 * sKiB);
                ASSERT_TRUE(small.has_value());
                EXPECT_FALSE(pool.reserve(512 * sKiB).has_value()) << "past the most beside a charge";
            }

            PooledBuffer past = bufferOf(pool, 512 * sKiB);
            EXPECT_EQ(past.bytes().size(), 512 * sKiB);
            EXPECT_EQ(pool.getHeld().mCharged, 512 * sKiB) << "made alone past the most";
            const std::size_t freeBefore = pool.getHeld().mFree;
            past = PooledBuffer();
            EXPECT_EQ(pool.getHeld().mFree, freeBefore + 512 * sKiB) << "freed on the thread that gave it back";
            EXPECT_EQ(pool.getHeld().mCharged, 0u);

            const PooledBuffer next = bufferOf(pool, 64 * sKiB);
            EXPECT_LE(pool.getHeld().mCharged + pool.getHeld().mFree, 256 * sKiB)
                << "the next spend left what stood past the most";
        }

        /// **A credit is charged until the buffer it is spent on goes back**: 100 KiB reserved charges its 128 KiB
        /// class, and the 10 bytes spent of it, a 64 KiB buffer, still charge 128 KiB until they go. One let go unspent
        /// charges nothing after; a refund gives the bytes back and keeps the charge, which a spend of the credit takes
        /// again, in the buffer given back.
        TEST(RtxBufferPoolTest, aCreditIsChargedUntilTheBufferItIsSpentOnGoesBack)
        {
            BufferPool pool(1024 * sKiB);
            {
                std::optional<PoolCredit> credit = pool.reserve(100 * sKiB);
                ASSERT_TRUE(credit.has_value());
                EXPECT_EQ(credit->getCharged(), 128 * sKiB);
                EXPECT_EQ(pool.getHeld().mCharged, 128 * sKiB);
            }
            EXPECT_EQ(pool.getHeld().mCharged, 0u) << "let go unspent";

            std::optional<PoolCredit> credit = pool.reserve(100 * sKiB);
            ASSERT_TRUE(credit.has_value());
            PooledBuffer small = std::move(*credit).spend(10);
            EXPECT_EQ(small.bytes().size(), 10u);
            EXPECT_EQ(pool.getHeld().mCharged, 128 * sKiB) << "the credit's charge, not the buffer's class";
            const std::byte* const held = small.bytes().data();

            PoolCredit refunded = std::move(small).refund();
            EXPECT_EQ(refunded.getCharged(), 128 * sKiB);
            EXPECT_EQ(pool.getHeld().mCharged, 128 * sKiB) << "a refund keeps the charge";
            EXPECT_EQ(pool.getHeld().mFree, 64 * sKiB) << "and gives the bytes back";

            PooledBuffer again = std::move(refunded).spend(20);
            EXPECT_EQ(again.bytes().data(), held) << "the buffer given back";
            again = PooledBuffer();
            EXPECT_EQ(pool.getHeld().mCharged, 0u);

            std::optional<PoolCredit> narrow = pool.reserve(10);
            ASSERT_TRUE(narrow.has_value());
            Testing::expectAssertDies([&] { std::ignore = std::move(*narrow).spend(100 * sKiB); },
                "a buffer past the credit it is spent from");
        }
    }
}
