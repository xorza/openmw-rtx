#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/driverprofile.hpp>
#include <components/rtxvulkan/device/pipelinereport.hpp>
#include <components/rtxvulkan/device/pipelinetally.hpp>

namespace Rtx
{
    namespace
    {
        using std::chrono::milliseconds;

        PipelineMade made(std::string_view name, PipelineSource source, double durationMs, std::uint32_t executables,
            std::uint64_t scratchBytes)
        {
            return PipelineMade{ .mName = name,
                .mFeedback = PipelineFeedback{ .mSource = source, .mDurationMs = durationMs },
                .mExecutables = executables,
                .mScratchBytes = scratchBytes };
        }

        /// **A stretch of pipelines in a line, and its spills in another**: compiled, cached and unsaid
        /// counted apart, the compiles summed with their slowest, the span reaching the last made
        /// whatever order they arrive in, and the spills the most first, a tie by name. A take starts
        /// the next stretch at its own instant. **A spill is scratch past what the driver holds at no
        /// cost**: at 32 bytes free the two pipelines at 32 are none, and at 112 none is.
        TEST(RtxPipelineTallyTest, aStretchIsSaidInALineAndItsSpillsInAnother)
        {
            const std::chrono::steady_clock::time_point start{};
            PipelineTally tally(start, 0);
            PipelineTally thirtyTwoFree(start, 32);
            PipelineTally allFree(start, 112);
            for (PipelineTally* const free : { &thirtyTwoFree, &allFree })
            {
                free->add(made("b", PipelineSource::Compiled, 250.5, 1, 32), start);
                free->add(made("e", PipelineSource::Cached, 0.3, 2, 112), start);
                free->add(made("f", PipelineSource::Compiled, 50.0, 1, 32), start);
            }
            EXPECT_EQ(describeSpills(thirtyTwoFree.take(start)),
                "1 of 3 pipelines use more than 32 bytes of scratch memory a lane: \"e\" 112");
            EXPECT_EQ(describeSpills(allFree.take(start)), "");

            tally.add(made("a", PipelineSource::Compiled, 100.0, 1, 0), start + milliseconds(500));
            tally.add(made("b", PipelineSource::Compiled, 250.5, 1, 32), start + milliseconds(1500));
            tally.add(made("c", PipelineSource::Cached, 0.2, 0, 0), start + milliseconds(1000));
            tally.add(made("d", PipelineSource::Unsaid, 0.0, 1, 0), start + milliseconds(250));
            tally.add(made("e", PipelineSource::Cached, 0.3, 2, 112), start + milliseconds(1250));
            tally.add(made("f", PipelineSource::Compiled, 50.0, 1, 32), start + milliseconds(750));

            const PipelineSummary first = tally.take(start + milliseconds(2000));
            EXPECT_EQ(first.mMade, 6u);
            EXPECT_EQ(first.mCompiled, 3u) << "a, b and f";
            EXPECT_EQ(first.mCached, 2u) << "c and e";
            // 100.0 + 250.5 + 50.0; a cached pipeline's duration is no compile.
            EXPECT_EQ(first.mCompileMs, 400.5);
            ASSERT_TRUE(first.mSlowest.has_value());
            EXPECT_EQ(first.mSlowest->mName, "b");
            EXPECT_EQ(first.mWithoutExecutable, 1u) << "c";
            EXPECT_EQ(first.mSpan.count(), 1.5) << "b, the last made, though added second";

            // 400.5 ms is 0.40 s; d is the one of 6 neither compiled nor cached.
            EXPECT_EQ(describePipelines(first),
                "6 made over 1.50 s: 3 compiled and 2 found in the pipeline cache, 1 the driver gave no feedback "
                "of; 0.40 s compiling, the slowest \"b\" at 250.50 ms; 1 with no executable the driver reports");
            EXPECT_EQ(describeSpills(first),
                "3 of 6 pipelines use more than 0 bytes of scratch memory a lane: \"e\" 112, \"b\" 32, \"f\" 32");

            EXPECT_EQ(describePipelines(tally.take(start + milliseconds(2000))), "none made") << "taken whole";

            // 2750 ms against the 2000 the last take started the stretch at.
            tally.add(made("g", PipelineSource::Cached, 0.1, 1, 0), start + milliseconds(2750));
            const PipelineSummary second = tally.take(start + milliseconds(3000));
            EXPECT_EQ(describePipelines(second), "1 made over 0.75 s: 0 compiled and 1 found in the pipeline cache");
            EXPECT_EQ(describeSpills(second), "") << "nothing spills, so nothing is warned of";
        }
        VkPipelineExecutableStatisticKHR statistic(const char* name, VkPipelineExecutableStatisticFormatKHR format)
        {
            VkPipelineExecutableStatisticKHR made{};
            made.sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
            std::strcpy(made.name, name);
            made.format = format;
            return made;
        }

        /// **An executable's statistics in the driver's order and words**, each format as itself, and the
        /// scratch figure alone masked to its bytes by the names it is read under: NVIDIA's 2^36 flag
        /// above 96 bytes reads 96 under NVIDIA's names and stands whole under a driver's that names no
        /// scratch.
        TEST(RtxPipelineReportTest, anExecutablesStatisticsAreSaidInTheDriversWords)
        {
            std::array statistics{
                statistic("Register Count", VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR),
                statistic("Local Memory Size", VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR),
                statistic("Spilled", VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR),
                statistic("Ratio", VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR),
                statistic("Offset", VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR),
            };
            statistics[0].value.u64 = 56;
            statistics[1].value.u64 = (std::uint64_t{ 1 } << 36) + 96;
            statistics[2].value.b32 = VK_TRUE;
            statistics[3].value.f64 = 0.5;
            statistics[4].value.i64 = -3;

            const StatisticNames nvidia{ .mScratch = "Local Memory Size",
                .mRegisters = "Register Count",
                .mShared = "Shared Memory Size",
                .mScratchBits = 0xFFFFFFFF };
            EXPECT_EQ(describeStatistics(statistics, nvidia),
                "Register Count 56, Local Memory Size 96, Spilled yes, Ratio 0.500000, Offset -3");
            // 2^36 + 96 = 68719476736 + 96.
            EXPECT_EQ(describeStatistics(statistics, StatisticNames{}),
                "Register Count 56, Local Memory Size 68719476832, Spilled yes, Ratio 0.500000, Offset -3");
            EXPECT_EQ(describeStatistics({}, nvidia), "");
        }
    }
}
