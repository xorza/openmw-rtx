#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <components/platform/thread.hpp>
#include <components/rtx/common/job.hpp>
#include <components/rtx/common/jobprogress.hpp>

namespace Rtx
{
    namespace
    {
        /// **Counted as it goes, short of the whole until it returns, and the whole after.** The
        /// work makes three of four steps and holds; the count says three of four however long a
        /// caller waits, and only the work's return makes it four.
        TEST(RtxJobTest, aJobIsCountedAsItGoesAndDoneOnlyOnceItReturns)
        {
            std::atomic<bool> release{ false };
            Job job;
            job.start("counted", 4, [&](const Platform::StopToken&, Job& self) {
                self.advance();
                self.advance();
                self.advance();
                while (!release)
                    std::this_thread::yield();
                self.advance();
            });

            JobProgress progress = job.waitFor(std::chrono::milliseconds(1));
            while (progress.mMade < 3)
                progress = job.waitFor(std::chrono::milliseconds(1));
            EXPECT_EQ(progress.mMade, 3u);
            EXPECT_EQ(progress.mCount, 4u);
            EXPECT_FALSE(progress.isDone());

            release = true;
            job.wait();
            progress = job.waitFor(std::chrono::milliseconds::zero());
            EXPECT_TRUE(progress.isDone());
            EXPECT_EQ(progress.mMade, 4u);
        }

        /// What the work threw is thrown to every wait, the bounded one included.
        TEST(RtxJobTest, whatTheWorkThrewIsThrownToEveryWait)
        {
            Job job;
            job.start("thrower", 1, [](const Platform::StopToken&, Job&) { throw std::runtime_error("broken"); });

            EXPECT_THROW(job.wait(), std::runtime_error);
            EXPECT_THROW(job.wait(), std::runtime_error);
            EXPECT_THROW(job.waitFor(std::chrono::hours(1)), std::runtime_error);
        }

        /// **A job going away stops its work** rather than waiting it out: the work runs until its
        /// token says stop, and the destruction is bounded at a second.
        TEST(RtxJobTest, aJobGoingAwayStopsItsWork)
        {
            std::atomic<bool> started{ false };
            std::atomic<bool> sawStop{ false };
            std::chrono::steady_clock::time_point began;
            {
                Job job;
                job.start("endless", 1, [&](const Platform::StopToken& stop, Job&) {
                    started = true;
                    while (!stop.stopRequested())
                        std::this_thread::yield();
                    sawStop = true;
                });
                while (!started)
                    std::this_thread::yield();
                began = std::chrono::steady_clock::now();
            }
            EXPECT_TRUE(sawStop);
            EXPECT_LT(std::chrono::steady_clock::now() - began, std::chrono::seconds(1));
        }
    }
}
