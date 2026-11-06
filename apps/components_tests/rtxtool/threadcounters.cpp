#include <array>
#include <chrono>
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

        /// **The counts are scaled to the thread's run, and the run is the time enabled**, so the clock
        /// is the thread's whatever share the processor counted. One group counted 5e8 ns of a 1e9 ns
        /// run, with 2e9 cycles: counted half, the cycles are 4e9 over the 1e9 ns run, the 4.00 GHz
        /// the thread ran at, where the cycles over the counted time alone read 8.00. Two kinds of
        /// core counted whole, 6e8 ns on the performance group and 4e8 on the efficiency group of a
        /// 1e9 ns run, are 40% on efficiency cores, and the same counted half — 3e8 and 2e8 — are
        /// 40% too. A thread nothing counted says why.
        TEST(RtxThreadCountersTest, theCountsAreScaledToTheThreadsRun)
        {
            const std::array<GroupCounts, 1> shared{ GroupCounts{ .mCycles = 2'000'000'000,
                .mInstructions = 3'000'000'000,
                .mCacheMisses = 6'000'000,
                .mRunningNs = 500'000'000,
                .mEnabledNs = 1'000'000'000 } };
            const ThreadCounts half = countsOf(shared, true, false);
            ASSERT_TRUE(half.mRead);
            EXPECT_EQ(half.mCounted, 0.5);
            EXPECT_EQ(half.mCycles, 4'000'000'000u);
            EXPECT_EQ(half.mInstructions, 6'000'000'000u);
            EXPECT_EQ(half.mCacheMisses, 12'000'000u);
            EXPECT_EQ(half.mRunningNs, 1'000'000'000u);
            EXPECT_EQ(describeCounts(half),
                "host thread 4.00 GHz, 1.50 instructions a cycle, 2.00 cache misses a thousand instructions, "
                "counted 50% of the time");

            const auto kinds = [](std::uint64_t performance, std::uint64_t efficiency) {
                return std::array<GroupCounts, 2>{
                    GroupCounts{
                        .mCycles = 1, .mInstructions = 1, .mRunningNs = performance, .mEnabledNs = 1'000'000'000 },
                    GroupCounts{ .mCycles = 1,
                        .mInstructions = 1,
                        .mRunningNs = efficiency,
                        .mEnabledNs = 1'000'000'000,
                        .mEfficiency = true },
                };
            };
            const ThreadCounts whole = countsOf(kinds(600'000'000, 400'000'000), true, true);
            EXPECT_EQ(whole.mCounted, 1.0);
            EXPECT_EQ(whole.mEfficiencyNs, 400'000'000u);
            const ThreadCounts halved = countsOf(kinds(300'000'000, 200'000'000), true, true);
            EXPECT_EQ(halved.mCounted, 0.5);
            EXPECT_EQ(halved.mEfficiencyNs, 400'000'000u) << "the efficiency cores' time not scaled with the rest";

            EXPECT_FALSE(countsOf(kinds(0, 0), true, true).mRead);
            EXPECT_FALSE(countsOf({}, true, false).mRead);
        }

        /// **A loop twice as long counts twice the instructions**, to the start and stop around it,
        /// on whichever kind of core the thread ran: the counters are this thread's and nought at
        /// each start, and all of its running time is counted. Skipped where the system keeps the
        /// counters from the process, which says why.
        TEST(RtxThreadCountersTest, aLoopTwiceAsLongCountsTwiceTheInstructions)
        {
            ThreadCounters counters;
            std::uint64_t wallNs = 0;
            const auto counted = [&](const std::uint64_t turns) {
                const auto began = std::chrono::steady_clock::now();
                counters.start();
                spin(turns);
                const ThreadCounts counts = counters.stop();
                wallNs = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began)
                        .count());
                return counts;
            };

            // A window thrown away first, so neither measured one starts with the core still
            // climbing to its clock; and windows of milliseconds, so a tick of the scheduler is a
            // small part of either.
            if (const ThreadCounts warm = counted(10'000'000); !warm.mRead)
                GTEST_SKIP() << warm.mWhyNot;
            const ThreadCounts once = counted(10'000'000);
            const ThreadCounts twice = counted(20'000'000);
            const std::uint64_t twiceWallNs = wallNs;
            ASSERT_TRUE(once.mRead);
            ASSERT_TRUE(twice.mRead);

            // **To a tenth, and to a hundredth of the time counted**: beside the other suites the
            // kernel's interrupts land in the thread's count, and each move between the two kinds of
            // core loses a sliver of it — measured at a ratio of 2.021 and 99.7% counted, and once at
            // 1.868 with a burst in the shorter window, against 2.000 and all of it on a quiet machine.
            EXPECT_GT(once.mInstructions, 10'000'000u) << "a turn is more than one instruction";
            EXPECT_NEAR(static_cast<double>(twice.mInstructions) / static_cast<double>(once.mInstructions), 2.0, 0.2);
            EXPECT_GT(once.mCycles, 0u);
            EXPECT_GT(once.mRunningNs, 0u);
            EXPECT_NEAR(once.mCounted, 1.0, 0.01) << "no other process counts on this thread's cores";

            // **Each window's time is its own**, and no longer than the wall clock around it: a reset
            // zeroes the counts and not the times, and without the start's subtracted a window read
            // every window's before it as well.
            EXPECT_LE(twice.mRunningNs, twiceWallNs) << "the window carried an earlier one's time";
            EXPECT_LE(once.mEfficiencyNs, once.mRunningNs);
        }
    }
}
