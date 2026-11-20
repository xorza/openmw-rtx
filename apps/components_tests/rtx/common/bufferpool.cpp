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

            BufferPool pool(1024 * sKiB);
            const std::byte* first = nullptr;
            {
                std::optional<PooledBuffer> buffer = pool.take(100 * sKiB, {});
                ASSERT_TRUE(buffer.has_value());
                EXPECT_EQ(buffer->bytes().size(), 100 * sKiB);
                first = buffer->bytes().data();
                EXPECT_EQ(pool.getHeld().mInUse, 128 * sKiB);
                EXPECT_EQ(pool.getHeld().mFree, 0u);
            }
            EXPECT_EQ(pool.getHeld().mInUse, 0u);
            EXPECT_EQ(pool.getHeld().mFree, 128 * sKiB);

            std::optional<PooledBuffer> again = pool.take(90 * sKiB, {});
            ASSERT_TRUE(again.has_value());
            EXPECT_EQ(again->bytes().data(), first) << "the buffer given back";
            EXPECT_EQ(again->bytes().size(), 90 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 0u);
            again->truncate(1);
            EXPECT_EQ(again->bytes().size(), 1u);
            again.reset();

            // **A hand-over allocates nothing once its class is warm**: a take and its giving back, every class
            // a 1 MiB pool has room for warmed first.
            EXPECT_EQ(Testing::allocationsIn([&] {
                for (std::size_t round = 0; round < 4; ++round)
                {
                    std::optional<PooledBuffer> taken = pool.take(100 * sKiB, {});
                    ASSERT_TRUE(taken.has_value());
                }
            }),
                0u);
        }

        /// **What a pool holds stays under its most**: in a pool of 256 KiB, a take of a 256 KiB class while a
        /// 128 KiB buffer is free drops the free one; a take while every byte is in use waits for one given back, and
        /// a stop ends the wait with nothing; one larger than the most is made where nothing else is in use, kept as it
        /// comes back, never freed on the giving thread, and dropped by the next take, on the taker's.
        TEST(RtxBufferPoolTest, whatAPoolHoldsStaysUnderItsMost)
        {
            BufferPool pool(256 * sKiB);
            std::optional<PooledBuffer> small = pool.take(128 * sKiB, {});
            ASSERT_TRUE(small.has_value());
            small.reset();
            EXPECT_EQ(pool.getHeld().mFree, 128 * sKiB);

            std::optional<PooledBuffer> whole = pool.take(256 * sKiB, {});
            ASSERT_TRUE(whole.has_value());
            EXPECT_EQ(pool.getHeld().mInUse, 256 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 0u) << "the free buffer of another class dropped for room";

            {
                std::stop_source stopping;
                std::thread stopper([&] { stopping.request_stop(); });
                EXPECT_FALSE(pool.take(64 * sKiB, stopping.get_token()).has_value()) << "a stop ends the wait";
                stopper.join();
            }

            std::optional<PooledBuffer> waited;
            std::thread waiter([&] { waited = pool.take(64 * sKiB, {}); });
            whole.reset();
            waiter.join();
            ASSERT_TRUE(waited.has_value()) << "the buffer given back made room";
            waited.reset();

            std::optional<PooledBuffer> past = pool.take(512 * sKiB, {});
            ASSERT_TRUE(past.has_value()) << "made alone past the most";
            EXPECT_EQ(past->bytes().size(), 512 * sKiB);
            const std::size_t freeBefore = pool.getHeld().mFree;
            past.reset();
            EXPECT_EQ(pool.getHeld().mFree, freeBefore + 512 * sKiB) << "freed on the thread that gave it back";
            EXPECT_EQ(pool.getHeld().mInUse, 0u);

            // **And made once the pool stands empty where it met a buffer in use**: the wait ends on the same word the
            // take is made on, where it once left "nothing else in use" out and never ended. Whichever of the two
            // threads runs first, the take returns.
            std::optional<PooledBuffer> held = pool.take(64 * sKiB, {});
            ASSERT_TRUE(held.has_value());
            EXPECT_LE(pool.getHeld().mInUse + pool.getHeld().mFree, 256 * sKiB)
                << "the next take left what stood past the most";
            std::optional<PooledBuffer> large;
            std::thread larger([&] { large = pool.take(512 * sKiB, {}); });
            held.reset();
            larger.join();
            ASSERT_TRUE(large.has_value()) << "a take past the most waited past the pool standing empty";
            EXPECT_EQ(large->bytes().size(), 512 * sKiB);
        }

        /// **An overdraft lets a take past the most while it stands**, for the waiter that holds what is taken until it
        /// has it all: a take that waited on a full pool is made as one begins, and one after it ends waits again. In
        /// a pool of 128 KiB, two 64 KiB buffers fill it; the third waits until the overdraft, and the fourth, past it,
        /// until a stop.
        TEST(RtxBufferPoolTest, anOverdraftLetsATakePastTheMostWhileItStands)
        {
            BufferPool pool(128 * sKiB);
            std::optional<PooledBuffer> first = pool.take(64 * sKiB, {});
            std::optional<PooledBuffer> second = pool.take(64 * sKiB, {});
            ASSERT_TRUE(first.has_value());
            ASSERT_TRUE(second.has_value());

            std::optional<PooledBuffer> third;
            std::thread waiter([&] { third = pool.take(64 * sKiB, {}); });
            {
                const BufferPool::Overdraft overdraft(pool);
                waiter.join();
                ASSERT_TRUE(third.has_value()) << "a take waited on a full pool through an overdraft";
                EXPECT_EQ(pool.getHeld().mInUse, 192 * sKiB);
                EXPECT_TRUE(pool.take(64 * sKiB, {}).has_value()) << "a take past the most waited under an overdraft";
            }

            std::stop_source stopping;
            std::thread stopper([&] { stopping.request_stop(); });
            EXPECT_FALSE(pool.take(64 * sKiB, stopping.get_token()).has_value())
                << "a take past the most was made once the overdraft ended";
            stopper.join();

            // The two made past the most are kept as they came back, past the most, and the next take drops them.
            third.reset();
            EXPECT_EQ(pool.getHeld().mInUse, 128 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 128 * sKiB);
            first.reset();
            const std::optional<PooledBuffer> again = pool.take(64 * sKiB, {});
            ASSERT_TRUE(again.has_value());
            EXPECT_EQ(pool.getHeld().mInUse, 128 * sKiB);
            EXPECT_EQ(pool.getHeld().mFree, 0u) << "a take left what stood past the most";
        }
    }
}
