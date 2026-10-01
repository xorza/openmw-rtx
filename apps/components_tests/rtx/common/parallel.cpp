#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/thread.hpp>
#include <components/rtx/common/parallel.hpp>

namespace Rtx
{
    namespace
    {
        /// Every index runs exactly once, whichever hand took it.
        TEST(RtxParallelTest, everyIndexRunsOnceAndTheRunWaitsForAllOfThem)
        {
            constexpr std::size_t count = 64;
            std::vector<std::atomic<int>> ran(count);

            runInParallel(
                "parallel", count, Platform::StopToken{}, [] { return 0; }, [&](std::size_t at) { ++ran[at]; });

            for (std::size_t at = 0; at < count; ++at)
                EXPECT_EQ(ran[at].load(), 1) << "index " << at;
        }

        /// The first throw is what comes back, and every other index still runs.
        ///
        /// Index nought throws and the rest count themselves, so the count proves the run was not
        /// abandoned at the throw.
        TEST(RtxParallelTest, theFirstThrowComesBackAndTheRestOfTheBatchStillRuns)
        {
            constexpr std::size_t count = 32;
            std::atomic<std::size_t> ran{ 0 };

            const auto run = [&] {
                runInParallel(
                    "parallel", count, Platform::StopToken{}, [] { return 0; },
                    [&](std::size_t at) {
                        if (at == 0)
                            throw std::runtime_error("nought");

                        ++ran;
                    });
            };

            EXPECT_THROW(run(), std::runtime_error);
            EXPECT_EQ(ran.load(), count - 1);
        }

        /// A body with state of its own: each copy counts the turns it took and adds them to the
        /// total when the hand it belongs to is done with it.
        struct Counting
        {
            explicit Counting(std::atomic<std::size_t>& total)
                : mTotal(&total)
            {
            }

            /// **The count is not copied.** A copy is made before the hand it is for has run, so it
            /// starts from nothing and reports only what it went on to do.
            Counting(const Counting& other)
                : mTotal(other.mTotal)
            {
            }

            Counting& operator=(const Counting&) = delete;

            ~Counting() { mTotal->fetch_add(mMine); }

            /// Read, let the other hands in, then write. **Its own field, so nothing here is owed
            /// a lock** — and the gap is what makes a body two hands shared lose what it counted
            /// every time rather than once in a while.
            void operator()(std::size_t)
            {
                const std::size_t was = mMine;
                std::this_thread::yield();
                mMine = was + 1;
            }

            std::atomic<std::size_t>* mTotal;
            std::size_t mMine = 0;
        };

        /// Each hand runs its own copy of the body, so a body that keeps state is not a race.
        ///
        /// **One callable invoked from every hand at once is undefined**, and nothing in the type
        /// system says so: a body whose `operator()` is const hides it, which is what every caller
        /// here happens to pass. The unlocked `++mMine` below is the statement of the contract, and
        /// the sum is exact only where no two hands shared the object it belongs to.
        TEST(RtxParallelTest, eachHandRunsItsOwnCopyOfTheBody)
        {
            constexpr std::size_t count = 64;
            std::atomic<std::size_t> counted{ 0 };

            runInParallel(
                "parallel", count, Platform::StopToken{}, [] { return 0; }, Counting(counted));

            EXPECT_EQ(counted.load(), count) << "the hands shared one body and lost what it counted";
        }

        /// Nothing to do is not a thread.
        TEST(RtxParallelTest, aRunOfNothingStartsNothing)
        {
            std::atomic<std::size_t> equipped{ 0 };

            runInParallel(
                "parallel", 0, Platform::StopToken{}, [&] { return ++equipped; },
                [](std::size_t) { FAIL() << "a body ran for no index"; });

            EXPECT_EQ(equipped.load(), 0u);
        }

        /// **A stop asked of the caller's thread ends the batch early.** A million indices of a
        /// millisecond each would take minutes; the stop comes after the first and the batch is back
        /// within a second, with most of it never run.
        TEST(RtxParallelTest, aStopEndsTheBatchEarly)
        {
            constexpr std::size_t count = 1'000'000;
            std::atomic<std::size_t> ran{ 0 };
            std::atomic<bool> returned{ false };

            Platform::Thread runner("runner", [&](Platform::StopToken stop) {
                runInParallel(
                    "parallel", count, stop, [] { return 0; },
                    [&](std::size_t) {
                        ++ran;
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    });
                returned = true;
            });
            while (ran == 0)
                std::this_thread::yield();

            const auto began = std::chrono::steady_clock::now();
            runner.stop();
            EXPECT_TRUE(returned);
            EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(1));
            EXPECT_LT(ran.load(), count);
        }

        /// **The caller is one of the hands**, so a batch never leaves the thread that asked for it
        /// idle. Each index holds its hand a millisecond, and there are four for every hand, so
        /// the caller cannot miss them all.
        TEST(RtxParallelTest, theCallerIsOneOfTheHands)
        {
            const std::size_t count = 4 * std::max(1u, std::thread::hardware_concurrency());
            const std::thread::id caller = std::this_thread::get_id();
            std::atomic<std::size_t> byCaller{ 0 };

            runInParallel(
                "parallel", count, Platform::StopToken{}, [] { return 0; },
                [&](std::size_t) {
                    if (std::this_thread::get_id() == caller)
                        ++byCaller;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });

            EXPECT_GT(byCaller.load(), 0u);
        }

        /// **What a hand's equipment throws comes back as a body's would**, and that hand takes no
        /// index while the others take every one. The caller's equipment is the one that throws.
        TEST(RtxParallelTest, aThrowFromAHandsEquipmentComesBack)
        {
            if (std::thread::hardware_concurrency() < 2)
                GTEST_SKIP() << "one hand, which is the caller's";

            constexpr std::size_t count = 64;
            const std::thread::id caller = std::this_thread::get_id();
            std::atomic<std::size_t> ran{ 0 };

            const auto run = [&] {
                runInParallel(
                    "parallel", count, Platform::StopToken{},
                    [caller] {
                        if (std::this_thread::get_id() == caller)
                            throw std::runtime_error("unequipped");
                        return 0;
                    },
                    [&](std::size_t) { ++ran; });
            };

            EXPECT_THROW(run(), std::runtime_error);
            EXPECT_EQ(ran.load(), count);
        }
    }
}
