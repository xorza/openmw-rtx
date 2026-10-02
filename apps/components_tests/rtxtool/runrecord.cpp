#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/files/conversion.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A run's status tells "differed" from "failed"**: a run that only differed from its
        /// reference answers `sDifferedStatus`, one that failed answers 1 whatever else differed, and
        /// a failed check is a failure.
        TEST(RtxRunRecordTest, aRunThatDifferedIsToldApartFromOneThatFailed)
        {
            EXPECT_EQ(RunRecord().describe(nullptr).mExitStatus, 0);

            RunRecord differed;
            differed.differ();
            EXPECT_EQ(differed.describe(nullptr).mExitStatus, sDifferedStatus);

            RunRecord both;
            both.differ();
            both.fail();
            EXPECT_EQ(both.describe(nullptr).mExitStatus, 1);

            RunRecord checked;
            checked.checked(true);
            EXPECT_EQ(checked.describe(nullptr).mExitStatus, 0);
            checked.checked(false);
            EXPECT_EQ(checked.describe(nullptr).mExitStatus, 1);
        }

        /// **A record that cannot be written fails the run and keeps its report.** The close runs
        /// inside the engine's frame, and a throw from there lost every measured place with the
        /// report it was in; a path nobody can write is one line under them and a status of 1.
        TEST(RtxRunRecordTest, aFileItCannotWriteFailsTheRunAndKeepsTheReport)
        {
            const std::filesystem::path nowhere = TestingOpenMW::outputFilePath("no-such-folder") / "deeper";
            std::filesystem::remove_all(nowhere.parent_path());

            RunRecord record;
            BenchPlace measured;
            measured.mView = "seyda-neen";
            record.add(measured);
            const std::string place = record.getReport();

            SessionRequest request;
            request.mHashes = nowhere / "hashes.csv";
            request.mJson = nowhere / "record.json";
            record.finish(request);

            const SessionResult result = record.describe(nullptr);
            EXPECT_EQ(result.mExitStatus, 1);
            EXPECT_EQ(result.mReport.find(place), 0u) << result.mReport;
            EXPECT_NE(result.mReport.find("could not write " + Files::pathToUnicodeString(nowhere / "hashes.csv")),
                std::string::npos)
                << result.mReport;
            EXPECT_NE(result.mReport.find("could not write " + Files::pathToUnicodeString(nowhere / "record.json")),
                std::string::npos)
                << result.mReport;
            EXPECT_EQ(result.mReport.find("wrote"), std::string::npos) << result.mReport;
        }
    }
}
