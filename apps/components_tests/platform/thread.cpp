#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/thread.hpp>

namespace Platform
{
    namespace
    {
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
                const std::jthread thread = startThread("cell reader", [&] {
                    named = nameOfThisThread();
                    seen = std::get_terminate();
                });
            }
            {
                const std::jthread thread = startThread("a name of twenty chars", [&] { cut = nameOfThisThread(); });
            }
            std::set_terminate(before);

            EXPECT_EQ(named, "cell reader");
            EXPECT_EQ(cut, "a name of twent") << "fifteen characters on every system";
            EXPECT_EQ(seen, ours);
        }

        /// **A thread's CPU time is what it ran, not what it waited**: a sleep of 50 ms adds less than the
        /// sleep, and a spin moves it by at least a millisecond within a second of the wall. Windows counts
        /// it in ticks of 15.6 ms, so the spin's bound waits out a tick, and a tick's credit may pass the
        /// wall the spin took, which no assertion here reads.
        TEST(PlatformThreadTest, aThreadsCpuTimeCountsWorkAndNotASleep)
        {
            const std::chrono::nanoseconds beforeSleep = cpuTimeOfThisThread();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            EXPECT_LT(cpuTimeOfThisThread() - beforeSleep, std::chrono::milliseconds(50));

            const auto began = std::chrono::steady_clock::now();
            const std::chrono::nanoseconds beforeSpin = cpuTimeOfThisThread();
            std::chrono::nanoseconds spun{};
            while (spun < std::chrono::milliseconds(1)
                && std::chrono::steady_clock::now() - began < std::chrono::seconds(1))
                spun = cpuTimeOfThisThread() - beforeSpin;
            EXPECT_GE(spun, std::chrono::milliseconds(1)) << "a second's spin ran no CPU";
        }

        /// A stop ends the sleep at once and says so; one asked before the sleep ends it before it
        /// starts; a token with no stop state sleeps the period out. Bounds of a second, against a
        /// period of a minute, so a loaded machine cannot pass the wrong answer.
        TEST(PlatformThreadTest, aStopEndsTheSleepAndSaysSo)
        {
            const auto now = [] { return std::chrono::steady_clock::now(); };

            EXPECT_TRUE(sleepUnlessStopped(std::stop_token(), std::chrono::milliseconds(1)));

            std::atomic<bool> sleeping = false;
            std::atomic<bool> slept = true;
            const auto began = now();
            {
                const std::jthread thread = startThread("sleeper", [&](const std::stop_token& stop) {
                    sleeping = true;
                    slept = sleepUnlessStopped(stop, std::chrono::minutes(1));
                });
                while (!sleeping)
                    std::this_thread::yield();
            }
            EXPECT_FALSE(slept);
            EXPECT_LT(now() - began, std::chrono::seconds(1)) << "the stop did not end the sleep";

            std::stop_source asked;
            asked.request_stop();
            const auto again = now();
            EXPECT_FALSE(sleepUnlessStopped(asked.get_token(), std::chrono::minutes(1)));
            EXPECT_LT(now() - again, std::chrono::seconds(1));
        }

        /// **Every hand is asked to stop before the first is joined**: three threads that each, once stopped, wait
        /// until all three have been, end; joined one at a time, the first would wait for a stop the second was never
        /// asked. Each waits a bounded while, so a broken order fails rather than hangs.
        TEST(PlatformThreadTest, everyThreadIsAskedToStopBeforeAnyIsJoined)
        {
            std::atomic<int> stopped{ 0 };
            std::atomic<int> waitedOut{ 0 };
            std::vector<std::jthread> hands;
            for (int at = 0; at < 3; ++at)
                hands.push_back(startThread("hand", [&](const std::stop_token& stop) {
                    while (!stop.stop_requested())
                        std::this_thread::yield();
                    ++stopped;
                    const auto due = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    while (stopped.load() < 3)
                    {
                        if (std::chrono::steady_clock::now() > due)
                        {
                            ++waitedOut;
                            return;
                        }
                        std::this_thread::yield();
                    }
                }));

            stopThenJoin(hands, [](std::jthread& hand) -> std::jthread& { return hand; });
            EXPECT_EQ(stopped.load(), 3);
            EXPECT_EQ(waitedOut.load(), 0) << "a hand was joined before the others were asked to stop";
            for (const std::jthread& hand : hands)
                EXPECT_FALSE(hand.joinable());
        }
    }
}
