#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <thread>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/bound.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/common/job.hpp>
#include <components/rtx/common/jobprogress.hpp>

namespace Rtx
{
    namespace
    {
        /// **Held short of the count until finished**, whatever the work counted: five of four
        /// counted reads three of four, two reads two, and a finished job reads its whole count
        /// whatever it counted, a job of no steps among them.
        TEST(RtxJobTest, progressIsHeldShortOfTheCountUntilFinished)
        {
            struct Row
            {
                std::uint32_t mMade;
                std::uint32_t mCount;
                bool mFinished;
                std::uint32_t mRead;
            };
            constexpr std::array rows{
                Row{ 5, 4, false, 3 },
                Row{ 2, 4, false, 2 },
                Row{ 0, 4, true, 4 },
                Row{ 0, 0, true, 0 },
                Row{ 0, 0, false, 0 },
            };
            for (const Row& row : rows)
            {
                const JobProgress read = JobProgress::of(row.mMade, row.mCount, row.mFinished);
                EXPECT_EQ(read.mMade, row.mRead) << row.mMade << " of " << row.mCount;
                EXPECT_EQ(read.mCount, row.mCount);
                EXPECT_EQ(read.isDone(), row.mFinished || row.mCount == 0) << row.mMade << " of " << row.mCount;
            }
        }

        /// **Counted as it goes, short of the whole until it returns, and the whole after.** The
        /// work makes three of four steps and holds; the count says three of four however long a
        /// caller waits, and only the work's return makes it four.
        TEST(RtxJobTest, aJobIsCountedAsItGoesAndDoneOnlyOnceItReturns)
        {
            std::atomic<bool> release{ false };
            Job job;
            job.start("counted", 4, [&](const std::stop_token&, Job& self) {
                self.advance();
                self.advance();
                self.advance();
                for (const Testing::WaitBound bound(Testing::sHandOverPatience); !release;)
                {
                    if (bound.passed())
                    {
                        ADD_FAILURE() << "the job was never released";
                        break;
                    }
                    std::this_thread::yield();
                }
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
            job.start("thrower", 1, [](const std::stop_token&, Job&) { throw std::runtime_error("broken"); });

            EXPECT_THROW(job.wait(), std::runtime_error);
            EXPECT_THROW(job.wait(), std::runtime_error);
            EXPECT_THROW(job.waitFor(std::chrono::hours(1)), std::runtime_error);
        }

        /// **A job going away stops its work** rather than waiting it out: the work runs until its
        /// token says stop, which only the job's going away asks.
        TEST(RtxJobTest, aJobGoingAwayStopsItsWork)
        {
            std::atomic<bool> started{ false };
            std::atomic<bool> sawStop{ false };
            {
                Job job;
                job.start("endless", 1, [&](const std::stop_token& stop, Job&) {
                    started = true;
                    for (const Testing::WaitBound bound(Testing::sHandOverPatience); !stop.stop_requested();)
                    {
                        if (bound.passed())
                        {
                            ADD_FAILURE() << "the job was never asked to stop";
                            break;
                        }
                        std::this_thread::yield();
                    }
                    sawStop = true;
                });
                for (const Testing::WaitBound bound(Testing::sHandOverPatience); !started;)
                {
                    ASSERT_FALSE(bound.passed()) << "the job never started";
                    std::this_thread::yield();
                }
                EXPECT_FALSE(sawStop) << "the work stopped before the job went away";
            }
            EXPECT_TRUE(sawStop);
        }
    }
}
