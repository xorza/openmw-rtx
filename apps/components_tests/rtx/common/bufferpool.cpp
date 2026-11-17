#include <cstddef>
#include <optional>
#include <stop_token>
#include <thread>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <components/rtx/common/bufferpool.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::size_t sKiB = 1024;

        /// **A take is made in its class, and a buffer given back is the next take's**: 100 KiB is a 128 KiB class,
        /// 10 bytes the smallest, 64 KiB, and 64 KiB itself; a buffer given back is held free, and the next take of its
        /// class is that very buffer, so nothing more is made.
        TEST(RtxBufferPoolTest, aTakeIsMadeInItsClassAndABufferGivenBackIsTheNextTakes)
        {
            EXPECT_EQ(BufferPool::classBytesOf(100 * sKiB), 128 * sKiB);
            EXPECT_EQ(BufferPool::classBytesOf(10), 64 * sKiB);
            EXPECT_EQ(BufferPool::classBytesOf(64 * sKiB), 64 * sKiB);

            const std::shared_ptr<BufferPool> pool = BufferPool::make(1024 * sKiB);
            const std::byte* first = nullptr;
            {
                std::optional<PooledBuffer> buffer = pool->take(100 * sKiB, {});
                ASSERT_TRUE(buffer.has_value());
                EXPECT_EQ(buffer->bytes().size(), 100 * sKiB);
                first = buffer->bytes().data();
                EXPECT_EQ(pool->getHeld().mInUse, 128 * sKiB);
                EXPECT_EQ(pool->getHeld().mFree, 0u);
            }
            EXPECT_EQ(pool->getHeld().mInUse, 0u);
            EXPECT_EQ(pool->getHeld().mFree, 128 * sKiB);

            std::optional<PooledBuffer> again = pool->take(90 * sKiB, {});
            ASSERT_TRUE(again.has_value());
            EXPECT_EQ(again->bytes().data(), first) << "the buffer given back";
            EXPECT_EQ(again->bytes().size(), 90 * sKiB);
            EXPECT_EQ(pool->getHeld().mFree, 0u);
            again->truncate(1);
            EXPECT_EQ(again->bytes().size(), 1u);
            again.reset();

            // **A hand-over allocates nothing once its class is warm**: a take and its giving back, every class
            // a 1 MiB pool has room for warmed first.
            EXPECT_EQ(Testing::allocationsIn([&] {
                for (std::size_t round = 0; round < 4; ++round)
                {
                    std::optional<PooledBuffer> taken = pool->take(100 * sKiB, {});
                    ASSERT_TRUE(taken.has_value());
                }
            }),
                0u);
        }

        /// **What a pool holds stays under its most**: in a pool of 256 KiB, a take of a 256 KiB class while a
        /// 128 KiB buffer is free drops the free one; a take while every byte is in use waits for one given back, and
        /// a stop ends the wait with nothing; one larger than the most is made where nothing else is in use, and let
        /// go of as it comes back, as it does not fit beside what is held.
        TEST(RtxBufferPoolTest, whatAPoolHoldsStaysUnderItsMost)
        {
            const std::shared_ptr<BufferPool> pool = BufferPool::make(256 * sKiB);
            std::optional<PooledBuffer> small = pool->take(128 * sKiB, {});
            ASSERT_TRUE(small.has_value());
            small.reset();
            EXPECT_EQ(pool->getHeld().mFree, 128 * sKiB);

            std::optional<PooledBuffer> whole = pool->take(256 * sKiB, {});
            ASSERT_TRUE(whole.has_value());
            EXPECT_EQ(pool->getHeld().mInUse, 256 * sKiB);
            EXPECT_EQ(pool->getHeld().mFree, 0u) << "the free buffer of another class dropped for room";

            {
                std::stop_source stopping;
                std::thread stopper([&] { stopping.request_stop(); });
                EXPECT_FALSE(pool->take(64 * sKiB, stopping.get_token()).has_value()) << "a stop ends the wait";
                stopper.join();
            }

            std::optional<PooledBuffer> waited;
            std::thread waiter([&] { waited = pool->take(64 * sKiB, {}); });
            whole.reset();
            waiter.join();
            ASSERT_TRUE(waited.has_value()) << "the buffer given back made room";
            waited.reset();

            std::optional<PooledBuffer> past = pool->take(512 * sKiB, {});
            ASSERT_TRUE(past.has_value()) << "made alone past the most";
            EXPECT_EQ(past->bytes().size(), 512 * sKiB);
            past.reset();
            EXPECT_LE(pool->getHeld().mFree, 256 * sKiB) << "let go of as it came back";
            EXPECT_EQ(pool->getHeld().mInUse, 0u);
        }
    }
}
