#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
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

        /// **The report opens with what the run stood under, and the record writes it whole**: the
        /// build, the layers, whether the command measures, hashes or turns the weather, and every
        /// knob the renderer was made with. Two records that were not one run twice then cannot be
        /// read as an A/B, and a table a command took while it looked at its frames says so.
        TEST(RtxRunRecordTest, theReportOpensWithWhatTheRunStoodUnder)
        {
            Stop turning;
            turning.mSky.mTurnThrough = { Rtx::sWeatherRain };

            SessionRequest request;
            request.mStops = { turning };
            request.mHashes = TestingOpenMW::outputFilePath("premises-hashes.csv");
            request.mJson = TestingOpenMW::outputFilePath("premises.json");
            Rtx::RenderProfile& profile = request.mSetup.mRun.mProfile;
            profile.mReconstruction.mDenoise = false;
            profile.mReconstruction.mJitter = false;
            profile.mDelight = 0.5f;
            profile.mGamma = 2.2f;
            profile.mShow = Rtx::SurfaceView::Albedo;
            profile.mExposure = Rtx::FixedExposure{ 1.5f };
            profile.mSpecializeLaunches = false;
            profile.mStressOverlapMs = 8.0;
            request.mSetup.mMirror.mReach = Rtx::LandReach{ .mCells = 4.0f, .mViewingDistance = 7168.0f };
            request.mSetup.mMirror.mDistantStatics = false;
            request.mStep = 0.0625f;
            request.mSetup.mSettled = false;
            request.mSetup.mRun.mMemoryBudget = 512ull * 1024 * 1024;

            RunRecord record;
            record.begin(request);
            BenchHeader& header = record.getHeader();
            header.mExtents = Rtx::FrameExtents{
                .mRenderWidth = 960, .mRenderHeight = 540, .mOutputWidth = 1920, .mOutputHeight = 1080
            };
            // **What a frame resolved, not what the line asked**: the line asked no jitter, and an
            // upscaler jitters the ray whatever it is asked.
            header.mReconstruction = Rtx::Reconstruction{ .mDenoised = false,
                .mUpscale = Rtx::Upscale::Performance,
                .mJitter = true,
                .mNoise = Rtx::NoiseSource::WhiteHash,
                .mLevelBias = -1.0f,
                .mBounceReuse = Rtx::BounceReuse::Temporal,
                .mIndirect = Rtx::IndirectLight::Off,
                .mAntilag = true,
                .mHistoryFix = false,
                .mAntiFirefly = true };
            header.mValidating = true;

            BenchPlace place;
            place.mView = "seyda-neen";
            record.add(place);

            const std::string build = Rtx::sAssertsOn ? "a build with asserts, not one to quote" : "a release build";
            const std::string expected = "\nrun  " + build
                + ", layers on, not a figure to quote, not measured, every frame hashed, the weather turned\n"
                  "     1920x1080 from 960x540, upscale performance, filter off, jitter on, noise white-hash, "
                  "level bias -1.000, indirect off, bounce reuse temporal, antilag on, history fix off, anti-firefly on\n"
                  "     delight 0.50, gamma 2.20, show albedo, exposure fixed at 1.500, variants off, hold 8.0 ms\n"
                  "     land 4.0 cells, viewing distance 7168, distant statics off, step 0.0625 s, walks streamed, "
                  "memory budget 512 MiB, host pages ";
            EXPECT_EQ(record.getReport().substr(0, expected.size()), expected) << record.getReport();

            // **This process's own share**, read as the first place is added: a share in whole per
            // cent, or the words for a system that does not say.
            const std::string_view pages = std::string_view(record.getReport()).substr(expected.size());
            const std::string_view word = pages.substr(0, pages.find('\n'));
            EXPECT_TRUE(word == "not said" || (word.ends_with("% huge") && word.size() >= 7)) << word;
            EXPECT_EQ(pages.substr(word.size(), 13), "\n\nseyda-neen\n") << record.getReport();
            const std::optional<float> share = record.getHeader().mHugePageShare;
            EXPECT_EQ(word == "not said", !share.has_value());
            EXPECT_NE(record.getReport().find("  not a measurement: this command draws its frames"), std::string::npos);

            record.finish(request);
            std::ostringstream read;
            read << std::ifstream(request.mJson).rdbuf();
            std::filesystem::remove(request.mJson);
            std::filesystem::remove(request.mHashes);

            const std::string json = read.str();
            constexpr std::string_view premises
                = R"("measures": false, "hashed": true, "turnsWeather": true, "hugePageShare": )";
            constexpr std::string_view setup
                = R"(  "filter": false, "jitter": false, "delight": 0.500, "gamma": 2.200, "show": "albedo", )"
                  R"("exposure": 1.5, "exposureHeld": false, "variants": false, "holdMs": 8.000,)";
            constexpr std::string_view mirror = R"(  "landCells": 4.0, "viewingDistance": 7168.0, )"
                                                R"("distantStatics": false, "step": 0.0625, "settled": false, )"
                                                R"("memoryBudget": 536870912,)";
            EXPECT_NE(json.find(premises), std::string::npos) << json;
            EXPECT_NE(json.find(setup), std::string::npos) << json;
            EXPECT_NE(json.find(mirror), std::string::npos) << json;
            EXPECT_NE(
                json.find(R"("indirect": "off", "bounceReuse": "temporal", "antilag": true, "historyFix": false, )"
                          R"("antiFirefly": true)"),
                std::string::npos)
                << json;
            EXPECT_NE(json.find(Rtx::sAssertsOn ? R"("asserts": true)" : R"("asserts": false)"), std::string::npos);
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
