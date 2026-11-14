#include <cstdint>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/exitstatus.hpp>
#include <apps/rtxtool/model/finding.hpp>
#include <apps/rtxtool/model/logtally.hpp>
#include <components/debug/debuglog.hpp>

namespace RtxTool
{
    namespace
    {
        /// **An error and a warning are each a finding, counted once a line with where it was first said**; the
        /// lines below a warning are none, a line's end of line is not its text, and two threads saying one line say
        /// it twice.
        TEST(RtxLogTallyTest, theLogsLinesAreCountedOnceWithWhereTheyWereFirstSaid)
        {
            LogTally tally;
            tally.at("leg-1", 4);
            tally.hear(Debug::Info, "Loading cell Balmora");
            tally.hear(Debug::Warning, "Ray tracing leaves out a texture \"tx_a.dds\"\n");
            tally.at("leg-2", 9);
            std::thread other([&] { tally.hear(Debug::Warning, "Ray tracing leaves out a texture \"tx_a.dds\""); });
            other.join();
            tally.hear(Debug::Error, "Failed to load a model");

            FindingTable table;
            tally.drainInto(table);
            ASSERT_EQ(table.getFindings().size(), 2u);
            const Finding& warning = table.getFindings()[0];
            EXPECT_EQ(warning.mKind, FindingKind::LogWarning);
            EXPECT_EQ(warning.mDetail, "Ray tracing leaves out a texture \"tx_a.dds\"");
            EXPECT_EQ(warning.mCount, 2u);
            EXPECT_EQ(warning.mPlace.mLeg, "leg-1");
            EXPECT_EQ(warning.mPlace.mFrame, 4u);
            const Finding& error = table.getFindings()[1];
            EXPECT_EQ(error.mKind, FindingKind::LogError);
            EXPECT_EQ(error.mPlace.mLeg, "leg-2");

            tally.drainInto(table);
            EXPECT_EQ(table.getFindings()[0].mCount, 2u) << "drained once";
        }

        /// **Past its bound a line is counted, not kept**, and handed over as one finding of its own.
        TEST(RtxLogTallyTest, pastItsBoundALineIsCounted)
        {
            LogTally tally;
            tally.at("leg-4", 7);
            for (std::uint32_t at = 0; at < LogTally::sMost + 1; ++at)
                tally.hear(Debug::Warning, std::to_string(at));
            tally.at("leg-5", 9);
            tally.hear(Debug::Warning, "said later");
            FindingTable table;
            tally.drainInto(table);
            EXPECT_EQ(table.getFindings().size(), LogTally::sMost + 1);
            EXPECT_EQ(table.getFindings().back().mKind, FindingKind::LogWarning);
            EXPECT_EQ(table.getFindings().back().mDetail, "lines past the log's table");
            EXPECT_EQ(table.getFindings().back().mCount, 2u);
            EXPECT_EQ(table.getFindings().back().mPlace.mLeg, "leg-4") << "where the first past the bound was said";
            EXPECT_EQ(table.getFindings().back().mPlace.mFrame, 7u);
            EXPECT_EQ(table.statusUnder({}), ExitStatus::Passed);
        }

        /// **An error past the bound stays an error**, and fails the tour as one held does.
        TEST(RtxLogTallyTest, anErrorPastItsBoundStaysAnError)
        {
            LogTally tally;
            for (std::uint32_t at = 0; at < LogTally::sMost; ++at)
                tally.hear(Debug::Warning, std::to_string(at));
            tally.hear(Debug::Error, "Failed to load a model");
            tally.hear(Debug::Warning, "past");
            FindingTable table;
            tally.drainInto(table);
            ASSERT_EQ(table.getFindings().size(), LogTally::sMost + 2);
            const Finding& errors = table.getFindings()[LogTally::sMost];
            EXPECT_EQ(errors.mKind, FindingKind::LogError);
            EXPECT_EQ(errors.mDetail, "lines past the log's table");
            EXPECT_EQ(errors.mCount, 1u);
            EXPECT_EQ(table.getFindings().back().mKind, FindingKind::LogWarning);
            EXPECT_EQ(table.getFindings().back().mCount, 1u);
            EXPECT_EQ(table.statusUnder({}), ExitStatus::Failed);
        }
    }
}
