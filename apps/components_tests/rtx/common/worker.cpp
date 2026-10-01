#include <atomic>
#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/worker.hpp>

namespace Rtx
{
    namespace
    {
        /// A guard holds the thread that built it, and takes the one that adopts it.
        TEST(RtxOwnedByTest, aGuardHoldsTheThreadThatBuiltItUntilAnotherAdoptsIt)
        {
            OwnedBy owner;
            owner.check();

            std::thread other([&] {
                owner.adopt();
                owner.check();
            });
            other.join();

            Testing::expectAssertDies([&] { owner.check(); }, "a member touched from the wrong thread");

            owner.adopt();
            owner.check();
        }

        /// A repeating worker ticks at once, starts only once, and stops within the period's wait
        /// rather than at its end: a period of a minute, against a stop bounded at a second.
        TEST(RtxWorkerTest, aRepeatingWorkerStopsInsideItsPeriod)
        {
            Worker worker;
            std::atomic<int> ticks{ 0 };
            EXPECT_TRUE(worker.repeat("ticker", std::chrono::minutes(1), [&] { ++ticks; }));
            EXPECT_FALSE(worker.repeat("ticker", std::chrono::minutes(1), [&] { ++ticks; })) << "already running";

            while (ticks == 0)
                std::this_thread::yield();

            const auto began = std::chrono::steady_clock::now();
            worker.stop();
            EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(1));
            EXPECT_EQ(ticks, 1);
            EXPECT_FALSE(worker.isRunning());
        }
    }
}
