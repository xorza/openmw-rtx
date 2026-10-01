#include <components/platform/thread.hpp>

#include <atomic>
#include <thread>

#include <gtest/gtest.h>

namespace Platform
{
    namespace
    {
        /// A thread's end asks its work to stop and waits for it: the work spins until its token
        /// says so, and has seen it by the time the destructor returns.
        TEST(PlatformThreadTest, aThreadAsksItsWorkToStopAndJoinsIt)
        {
            std::atomic<bool> sawStop = false;
            {
                const Thread thread([&](StopToken stop) {
                    while (!stop.stopRequested())
                        std::this_thread::yield();
                    sawStop = true;
                });
            }
            EXPECT_TRUE(sawStop);

            std::atomic<int> runs = 0;
            {
                const Thread thread([&] { ++runs; });
            }
            EXPECT_EQ(runs, 1) << "a work that takes no token runs once and is joined";

            EXPECT_FALSE(StopToken().stopRequested()) << "a token made by default";
        }

        /// A thread assigned over stops and joins the one it held, and the new one runs on.
        TEST(PlatformThreadTest, aThreadAssignedOverStopsTheOldWork)
        {
            std::atomic<bool> firstStopped = false;
            std::atomic<bool> secondStopped = false;
            Thread thread([&](StopToken stop) {
                while (!stop.stopRequested())
                    std::this_thread::yield();
                firstStopped = true;
            });

            thread = Thread([&](StopToken stop) {
                while (!stop.stopRequested())
                    std::this_thread::yield();
                secondStopped = true;
            });
            EXPECT_TRUE(firstStopped);
            EXPECT_FALSE(secondStopped);

            thread.stop();
            EXPECT_TRUE(secondStopped);
            EXPECT_FALSE(thread.joinable());
        }

        /// A callback runs once when the stop is asked, or at once where it was asked already, and
        /// not at all once destroyed.
        TEST(PlatformThreadTest, aCallbackRunsOnceTheStopIsAsked)
        {
            std::atomic<bool> registered = false;
            std::atomic<bool> release = false;
            std::atomic<int> before = 0;
            std::atomic<int> gone = 0;
            std::atomic<int> after = 0;
            StopToken kept;

            Thread thread([&](StopToken stop) {
                kept = stop;
                const StopCallback counted(stop, [&] { ++before; });
                {
                    const StopCallback destroyed(stop, [&] { ++gone; });
                }
                registered = true;
                while (!stop.stopRequested())
                    std::this_thread::yield();
                while (!release)
                    std::this_thread::yield();
            });
            while (!registered)
                std::this_thread::yield();

            // The flag is up before the callbacks run, as `std::stop_source` raises it, so the wait is
            // on the callback.
            std::thread asker([&] { thread.stop(); });
            while (before == 0)
                std::this_thread::yield();
            EXPECT_TRUE(kept.stopRequested());
            EXPECT_EQ(gone, 0);

            const StopCallback late(kept, [&] { ++after; });
            EXPECT_EQ(after, 1) << "registered once the stop was asked";

            release = true;
            asker.join();
            EXPECT_EQ(before, 1);
        }
    }
}
