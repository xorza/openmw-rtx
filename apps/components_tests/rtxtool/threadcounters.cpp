#include <cstdint>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/threadcounters.hpp>

namespace RtxTool
{
    namespace
    {
        /// Adds through a volatile, so each turn is the same few instructions however the optimizer
        /// sees the loop.
        std::uint64_t spin(const std::uint64_t turns)
        {
            volatile std::uint64_t sum = 0;
            for (std::uint64_t turn = 0; turn < turns; ++turn)
                sum = sum + turn;
            return sum;
        }

        /// **The line is the ratios of the counts**: 5e9 cycles over 1e9 ns run is 5.00 GHz, 8e9
        /// instructions over them 1.60 a cycle, 2.4e7 misses over 8e9 instructions 3.00 a thousand,
        /// and a window counted half the time says so. Without the kernel's cycles there is no clock
        /// to say, and nothing counted says why.
        TEST(RtxThreadCountersTest, theLineIsTheClockTheInstructionsACycleAndTheMissesAThousand)
        {
            ThreadCounts counts{
                .mCycles = 5'000'000'000,
                .mInstructions = 8'000'000'000,
                .mCacheMisses = 24'000'000,
                .mRunningNs = 1'000'000'000,
                .mCounted = 1.0,
                .mKernelCounted = true,
                .mRead = true,
            };
            EXPECT_EQ(describeCounts(counts),
                "host thread 5.00 GHz, 1.60 instructions a cycle, 3.00 cache misses a thousand instructions");

            counts.mCounted = 0.5;
            counts.mKernelCounted = false;
            EXPECT_EQ(describeCounts(counts),
                "host thread 1.60 instructions a cycle, 3.00 cache misses a thousand instructions, counted 50% of "
                "the time");

            // 2.5e8 of the 1e9 ns run on the efficiency cores is a quarter, said only of a part with
            // two kinds; nought of them is said too, since it is what the harness's pinning promises.
            counts.mCounted = 1.0;
            counts.mTwoKinds = true;
            counts.mEfficiencyNs = 250'000'000;
            EXPECT_EQ(describeCounts(counts),
                "host thread 1.60 instructions a cycle, 3.00 cache misses a thousand instructions, 25% on "
                "efficiency cores");
            counts.mEfficiencyNs = 0;
            EXPECT_EQ(describeCounts(counts),
                "host thread 1.60 instructions a cycle, 3.00 cache misses a thousand instructions, 0% on "
                "efficiency cores");

            EXPECT_EQ(describeCounts(ThreadCounts{ .mWhyNot = "no counters" }), "host thread not counted: no counters");
            EXPECT_EQ(describeCounts(ThreadCounts{ .mRead = true }),
                "host thread not counted: it ran nothing the counters saw");
        }

        /// **A loop twice as long counts twice the instructions**, to the start and stop around it,
        /// on whichever kind of core the thread ran: the counters are this thread's and nought at
        /// each start, and all of its running time is counted. Skipped where the system keeps the
        /// counters from the process, which says why.
        TEST(RtxThreadCountersTest, aLoopTwiceAsLongCountsTwiceTheInstructions)
        {
            ThreadCounters counters;
            const auto counted = [&](const std::uint64_t turns) {
                counters.start();
                spin(turns);
                return counters.stop();
            };

            const ThreadCounts once = counted(1'000'000);
            if (!once.mRead)
                GTEST_SKIP() << once.mWhyNot;
            const ThreadCounts twice = counted(2'000'000);
            ASSERT_TRUE(twice.mRead);

            EXPECT_GT(once.mInstructions, 1'000'000u) << "a turn is more than one instruction";
            EXPECT_NEAR(static_cast<double>(twice.mInstructions) / static_cast<double>(once.mInstructions), 2.0, 0.02);
            EXPECT_GT(once.mCycles, 0u);
            EXPECT_GT(once.mRunningNs, 0u);
            // To the microsecond between the two kinds' starts, of a window of milliseconds.
            EXPECT_NEAR(once.mCounted, 1.0, 1e-3) << "no other process counts on this thread's cores";
            EXPECT_LE(once.mEfficiencyNs, once.mRunningNs);
        }
    }
}
