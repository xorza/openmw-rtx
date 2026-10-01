#include <components/platform/thread.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>
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
                const Thread thread("test", [&](StopToken stop) {
                    while (!stop.stopRequested())
                        std::this_thread::yield();
                    sawStop = true;
                });
            }
            EXPECT_TRUE(sawStop);

            std::atomic<int> runs = 0;
            {
                const Thread thread("test", [&] { ++runs; });
            }
            EXPECT_EQ(runs, 1) << "a work that takes no token runs once and is joined";

            EXPECT_FALSE(StopToken().stopRequested()) << "a token made by default";
        }

        /// A thread assigned over stops and joins the one it held, and the new one runs on.
        TEST(PlatformThreadTest, aThreadAssignedOverStopsTheOldWork)
        {
            std::atomic<bool> firstStopped = false;
            std::atomic<bool> secondStopped = false;
            Thread thread("test", [&](StopToken stop) {
                while (!stop.stopRequested())
                    std::this_thread::yield();
                firstStopped = true;
            });

            thread = Thread("test", [&](StopToken stop) {
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

            Thread thread("test", [&](StopToken stop) {
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

        /// A thread starts under the name it was made with, cut to the fifteen characters Linux
        /// keeps, and under the terminate handler of the thread that made it: MSVC would otherwise
        /// start it on the default, which aborts and says nothing.
        TEST(PlatformThreadTest, aThreadStartsNamedAndUnderItsMakersTerminateHandler)
        {
            const std::terminate_handler before = std::set_terminate([] { std::abort(); });
            const std::terminate_handler ours = std::get_terminate();

            std::string named;
            std::string cut;
            std::terminate_handler seen = nullptr;
            {
                const Thread thread("cell reader", [&] {
                    named = nameOfThisThread();
                    seen = std::get_terminate();
                });
            }
            {
                const Thread thread("a name of twenty chars", [&] { cut = nameOfThisThread(); });
            }
            std::set_terminate(before);

            EXPECT_EQ(named, "cell reader");
            EXPECT_EQ(cut, "a name of twent") << "fifteen characters";
            EXPECT_EQ(seen, ours);
        }

        /// A stop ends the sleep at once and says so; one asked before the sleep ends it before it
        /// starts; a token made by default sleeps the period out. Bounds of a second, against a
        /// period of a minute, so a loaded machine cannot pass the wrong answer.
        TEST(PlatformThreadTest, aStopEndsTheSleepAndSaysSo)
        {
            const auto now = [] { return std::chrono::steady_clock::now(); };

            EXPECT_TRUE(sleepUnlessStopped(StopToken(), std::chrono::milliseconds(1)));

            std::atomic<bool> sleeping = false;
            std::atomic<bool> slept = true;
            const auto began = now();
            {
                const Thread thread("sleeper", [&](StopToken stop) {
                    sleeping = true;
                    slept = sleepUnlessStopped(stop, std::chrono::minutes(1));
                });
                while (!sleeping)
                    std::this_thread::yield();
            }
            EXPECT_FALSE(slept);
            EXPECT_LT(now() - began, std::chrono::seconds(1)) << "the stop did not end the sleep";

            StopToken asked;
            {
                const Thread thread("stopped", [&](StopToken stop) {
                    while (!stop.stopRequested())
                        std::this_thread::yield();
                    asked = stop;
                });
            }
            const auto again = now();
            EXPECT_FALSE(sleepUnlessStopped(asked, std::chrono::minutes(1)));
            EXPECT_LT(now() - again, std::chrono::seconds(1));
        }
    }
}
