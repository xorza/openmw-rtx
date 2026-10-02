#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/renderer.hpp>
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

        /// **A run that failed before its last stop is closed for the stops it reached.** It was
        /// left open, and the record of the run before it stood at the path to be compared as this
        /// one's. A frame noted and never pictured is dropped rather than written as a hash of
        /// nothing, and the run fails.
        TEST(RtxRunRecordTest, anAbandonedRunWritesWhatItReachedAndFails)
        {
            const std::filesystem::path hashes = TestingOpenMW::outputFilePath("abandoned-hashes.csv");
            {
                std::ofstream stale(hashes);
                stale << "the run before\n";
            }

            RunRecord record;
            record.getHashes().note("seyda-neen", 1, 10, ScenePartDigests{});
            Rtx::FrameResult finished;
            finished.mFrame = 10;
            finished.mDigest = Rtx::FrameDigest{};
            ASSERT_TRUE(record.getHashes().picture(finished).has_value());
            record.getHashes().note("seyda-neen", 2, 11, ScenePartDigests{});

            SessionRequest request;
            request.mHashes = hashes;
            record.abandon(request);

            EXPECT_EQ(record.describe(nullptr).mExitStatus, 1);
            EXPECT_EQ(record.getHashes().frameCount(), 1u) << "the frame whose picture never came";
            EXPECT_NE(record.getReport().find("wrote 1 frame hashes"), std::string::npos) << record.getReport();

            std::ostringstream read;
            read << std::ifstream(hashes).rdbuf();
            EXPECT_EQ(read.str().find("the run before"), std::string::npos) << read.str();
            EXPECT_NE(read.str().find("seyda-neen,1,"), std::string::npos) << read.str();
            std::filesystem::remove(hashes);
        }
    }
}
