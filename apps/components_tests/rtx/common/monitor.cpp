#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/common/monitor.hpp>
#include <components/rtx/common/worker.hpp>

namespace Rtx
{
    namespace
    {
        /// A queue over a monitor, which is the shape both of this renderer's workers take.
        ///
        /// **Held together rather than declared in each test**, so the worker is destroyed — and
        /// so joined — before the queue it reads, which is the rule `Worker` states.
        struct Served
        {
            Monitor mMonitor;
            std::deque<int> mPending;
            std::vector<int> mDone;

            /// What a turn does with what it took, so a test can make one throw or hold on the
            /// stop. Handed the token `serve` hands the turn.
            std::function<void(int, Platform::StopToken)> mTurn = [](int, Platform::StopToken) {};

            /// **Last, for the reason `Worker` gives.**
            Worker mWorker;

            void start()
            {
                mWorker.start("served", [this](Platform::StopToken stop) {
                    int took = 0;

                    mMonitor.serve(
                        stop, [this] { return !mPending.empty(); },
                        [&] {
                            took = mPending.front();
                            mPending.pop_front();
                        },
                        [&](Platform::StopToken turn) {
                            mTurn(took, turn);
                            mMonitor.hand([&] { mDone.push_back(took); });
                        });
                });
            }

            void give(int one)
            {
                mMonitor.give([&] { mPending.push_back(one); });
            }

            /// Waits for `count` and answers them.
            std::vector<int> awaitDone(std::size_t count)
            {
                mMonitor.await([&] { return mDone.size() >= count; });
                return mMonitor.under([&] { return mDone; });
            }
        };

        /// Work given before the loop starts is not lost, and the order it is taken in is the order
        /// it arrived.
        ///
        /// **Given before the start on purpose.** A worker that only ever looked at the queue when
        /// it was woken would sit there for ever with three pieces of work in front of it, and a
        /// test that gave after starting would race past that.
        TEST(RtxMonitorTest, workGivenBeforeAWorkerStartsIsStillTaken)
        {
            Served served;
            served.give(1);
            served.give(2);
            served.give(3);

            served.start();

            EXPECT_EQ(served.awaitDone(3), (std::vector<int>{ 1, 2, 3 }));
        }

        /// A stop breaks the wait and the loop comes back, which is what every join rests on.
        TEST(RtxMonitorTest, aStopEndsALoopThatIsWaitingForWork)
        {
            Served served;
            served.start();

            served.give(7);
            EXPECT_EQ(served.awaitDone(1), (std::vector<int>{ 7 }));

            // The join is the assertion: it returns only where the wait was broken.
            served.mWorker.stop();
        }

        /// **A turn that throws ends the process, and says what it threw.** Nothing catches it, so
        /// `std::terminate` is called on the worker's thread where it was thrown, and the crash
        /// catcher's report keeps that stack; a catch that carried the exception to the frame would
        /// hand over the message with the stack already unwound.
        TEST(RtxMonitorTest, aTurnThatThrowsEndsTheProcessNamingWhatItThrew)
        {
            Testing::expectDies(
                [] {
                    Served served;
                    served.mTurn
                        = [](int one, Platform::StopToken) { throw std::runtime_error("turn " + std::to_string(one)); };
                    served.start();
                    served.give(4);
                    served.awaitDone(1);
                },
                "turn 4");
        }

        /// A stop drops what is still queued rather than taking one more turn nobody collects.
        ///
        /// **The turn is what holds the join up.** A bake is tens of milliseconds, so a loop that
        /// went on from a wait the stop had already broken would make every shutdown wait one out.
        TEST(RtxMonitorTest, aStopLeavesQueuedWorkWhereItIs)
        {
            Served served;
            std::atomic<bool> running{ false };

            // Held until the stop is asked for, which is what puts the next two behind it.
            served.mTurn = [&](int, Platform::StopToken stop) {
                running = true;
                while (!stop.stopRequested())
                    std::this_thread::yield();
            };
            served.start();

            served.give(1);
            while (!running)
                std::this_thread::yield();

            served.give(2);
            served.give(3);

            served.mWorker.stop();

            // Read without the lock, because the join is what makes this the only thread left.
            EXPECT_EQ(served.mDone, (std::vector<int>{ 1 })) << "a stopped loop took work nobody was left to collect";
            EXPECT_EQ(served.mPending.size(), 2u);
        }

        /// **A wait for what nothing will hand over ends, and says so.** With no worker serving it
        /// answers at once; with one that finished everything it was given and waits idle, as soon
        /// as the worker has looked; with what it waited for, true.
        TEST(RtxMonitorTest, aWaitEndsWhereNoWorkerCanHelp)
        {
            Served unserved;
            EXPECT_FALSE(unserved.mMonitor.await([&] { return !unserved.mDone.empty(); })) << "nobody serves";

            Served served;
            served.start();
            served.give(1);
            EXPECT_TRUE(served.mMonitor.await([&] { return !served.mDone.empty(); }));
            EXPECT_FALSE(served.mMonitor.await([&] { return served.mDone.size() >= 2; }))
                << "the worker is idle with nothing given";
        }

        /// **A worker in a turn is not idle**, so a wait goes on while it works rather than taking
        /// an idleness that came before the turn for its answer. The turn is held until the waiting
        /// thread has had a quarter of a second to answer wrongly.
        TEST(RtxMonitorTest, aWaitOutlastsAWorkersTurn)
        {
            Served served;
            std::atomic<bool> turning{ false };
            std::atomic<bool> release{ false };
            served.mTurn = [&](int, Platform::StopToken) {
                turning = true;
                while (!release)
                    std::this_thread::yield();
            };
            served.start();
            served.give(1);
            while (!turning)
                std::this_thread::yield();

            std::atomic<bool> answered{ false };
            bool got = false;
            std::thread waiter([&] {
                got = served.mMonitor.await([&] { return !served.mDone.empty(); });
                answered = true;
            });

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            EXPECT_FALSE(answered) << "the wait ended while the worker was in its turn";

            release = true;
            waiter.join();
            EXPECT_TRUE(got);
        }
    }
}
