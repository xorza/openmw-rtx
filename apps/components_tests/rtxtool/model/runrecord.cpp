#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/png.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/exitstatus.hpp>
#include <apps/rtxtool/model/pictureshelf.hpp>
#include <apps/rtxtool/model/runrecord.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/common/asserts.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/surfaceview.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/cells/cellgrid.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/world/weather.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        /// Two parts of one run answer the worse of their two statuses, whichever is asked first: a refusal over a
        /// failure over a difference over a pass.
        TEST(RtxExitStatusTest, twoPartsOfARunAnswerTheWorse)
        {
            constexpr std::array sBestFirst{ ExitStatus::Passed, ExitStatus::Differed, ExitStatus::Failed,
                ExitStatus::Refused };
            for (std::size_t a = 0; a < sBestFirst.size(); ++a)
                for (std::size_t b = 0; b < sBestFirst.size(); ++b)
                    EXPECT_EQ(worseOf(sBestFirst[a], sBestFirst[b]), sBestFirst[std::max(a, b)]) << a << ' ' << b;

            // The codes a driver reads: 0 passed, 1 failed, 2 a usage error, 3 differed.
            EXPECT_EQ(codeOf(ExitStatus::Passed), 0);
            EXPECT_EQ(codeOf(ExitStatus::Failed), 1);
            EXPECT_EQ(codeOf(ExitStatus::Refused), 2);
            EXPECT_EQ(codeOf(ExitStatus::Differed), 3);
        }

        /// **The result carries what the run kept**: the pictures where the request kept them, and the extents the
        /// renderer made where one was made; a run that keeps none hands back an empty shelf.
        TEST(RtxRunRecordTest, theResultCarriesThePicturesKeptAndTheExtentsTraced)
        {
            const std::filesystem::path file = "place.png";
            const std::array<std::uint8_t, 4> pixel{ 1, 2, 3, 255 };

            SessionRequest keeping;
            keeping.mKeepsPictures = true;
            RunRecord kept;
            kept.begin(keeping);
            ASSERT_NE(kept.getShelf(), nullptr);
            kept.getShelf()->keep(file, 1, 1, pixel);
            const Rtx::FrameExtents made{
                .mRenderWidth = 638, .mRenderHeight = 359, .mOutputWidth = 957, .mOutputHeight = 539
            };
            kept.noteTraced(made);
            const SessionResult result = kept.conclude(nullptr);
            const PngImage* const found = result.mShelf.find(file);
            ASSERT_NE(found, nullptr);
            EXPECT_EQ(found->mWidth, 1u);
            ASSERT_TRUE(result.mTraced.has_value());
            EXPECT_EQ(result.mTraced->mRenderWidth, 638u);
            EXPECT_EQ(result.mTraced->mOutputHeight, 539u);

            RunRecord unkept;
            unkept.begin(SessionRequest{});
            EXPECT_EQ(unkept.getShelf(), nullptr);
            const SessionResult none = unkept.conclude(nullptr);
            EXPECT_EQ(none.mShelf.find(file), nullptr);
            EXPECT_FALSE(none.mTraced.has_value()) << "no renderer was made";
        }

        /// **A run's status tells "differed" from "failed"**: a run that only differed from its
        /// reference answers differed, one that failed answers failed whatever else differed, and
        /// a failed check is a failure.
        TEST(RtxRunRecordTest, aRunThatDifferedIsToldApartFromOneThatFailed)
        {
            EXPECT_EQ(RunRecord().conclude(nullptr).mExitStatus, ExitStatus::Passed);

            RunRecord differed;
            differed.differ();
            EXPECT_EQ(differed.conclude(nullptr).mExitStatus, ExitStatus::Differed);

            RunRecord both;
            both.differ();
            both.fail();
            EXPECT_EQ(both.conclude(nullptr).mExitStatus, ExitStatus::Failed);
            RunRecord bothAfter;
            bothAfter.fail();
            bothAfter.differ();
            EXPECT_EQ(bothAfter.conclude(nullptr).mExitStatus, ExitStatus::Failed) << "whichever came first";

            RunRecord checked;
            checked.checked(Check::Finite, true);
            EXPECT_EQ(checked.conclude(nullptr).mExitStatus, ExitStatus::Passed);
            EXPECT_TRUE(checked.takeFailedChecks().empty());
            checked.checked(Check::GroundStands, false);
            checked.checked(Check::Finite, false);
            EXPECT_EQ(checked.conclude(nullptr).mExitStatus, ExitStatus::Failed);
            EXPECT_EQ(checked.takeFailedChecks(), (std::vector<Check>{ Check::GroundStands, Check::Finite }));
            EXPECT_TRUE(checked.takeFailedChecks().empty()) << "taken once";

            // **A stop passed over fails the run where somebody named it**, and not where it came of every view.
            RunRecord passed;
            passed.skip(Stop{ .mName = "mournhold-arrival", .mWhenAbsent = WhenAbsent::Skips },
                R"(no interior is called "Mournhold, Royal Palace: Reception Area")");
            EXPECT_EQ(passed.conclude(nullptr).mExitStatus, ExitStatus::Passed);
            EXPECT_EQ(passed.getReport(),
                "\nmournhold-arrival: passed over, no interior is called \"Mournhold, Royal Palace: Reception Area\", "
                "which a run of every view does not fail on\n");
            passed.skip(Stop{ .mName = "mournhold-arrival" }, "no interior is called \"Mournhold\"");
            EXPECT_EQ(passed.conclude(nullptr).mExitStatus, ExitStatus::Failed)
                << "a named stop's absence fails the run";
            EXPECT_TRUE(passed.getReport().ends_with(
                "\nmournhold-arrival: passed over, no interior is called \"Mournhold\"\n"));
        }

        /// **The report opens with what the run stood under, and the record writes it whole**: the
        /// build, the layers, whether the command measures, hashes or turns the weather, and every
        /// knob the renderer was made with. Two records that were not one run twice then cannot be
        /// read as an A/B, and a table a command took while it looked at its frames says so.
        TEST(RtxRunRecordTest, theReportOpensWithWhatTheRunStoodUnder)
        {
            Stop turning;
            turning.mSky.mTurnThrough = { Rtx::Weather::Rain };
            turning.mActions.mHash = true;

            SessionRequest request;
            request.mStops = { turning };
            request.mHashes = TestingOpenMW::currentTestDirPath() / "premises-hashes.csv";
            request.mJson = TestingOpenMW::currentTestDirPath() / "premises.json";
            Rtx::RenderProfile& profile = request.mSetup.mRun.mProfile;
            profile.mReconstruction.mDenoise = false;
            profile.mReconstruction.mJitter = false;
            profile.mGamma = 2.2f;
            profile.mShow = Rtx::SurfaceView::Albedo;
            profile.mExposure = Rtx::FixedExposure{ 1.5f };
            profile.mSpecializeKernels = false;
            profile.mStressOverlapMs = 8.0;
            request.mSetup.mMirror.mReach = Rtx::LandReach{ .mCells = 4.0f, .mViewingDistance = 7168.0f };
            request.mSetup.mMirror.mDistantStatics = false;
            request.mStep = 0.0625f;
            request.mSetup.mSettled = false;
            request.mSetup.mRun.mMemoryBudget = 512ull * 1024 * 1024;
            request.mPlayed = true;

            RunRecord record;
            record.begin(request);
            BenchHeader& header = record.getHeader();
            header.mExtents = Rtx::FrameExtents{
                .mRenderWidth = 960, .mRenderHeight = 540, .mOutputWidth = 1920, .mOutputHeight = 1080
            };
            // **What a frame resolved, not what the line asked**: the line asked no jitter, and an
            // upscaler jitters the ray whatever it is asked.
            header.mReconstruction = Rtx::Reconstruction{ .mFilter = Rtx::LightFilter::None,
                .mUpscale = Rtx::Upscale::Performance,
                .mJitter = true,
                .mLevelBias = -1.0f,
                .mNoise = Rtx::NoiseSource::WhiteHash,
                .mSampling = { .mShadowFloor = 0.0625f,
                    .mLampCandidates = 4u,
                    .mIndirect = false,
                    .mBounceRays = 3u,
                    .mRelief = false },
                .mFilters = { .mAntilag = true, .mHistoryFix = false, .mDualMotion = true, .mAntiFirefly = true },
                .mSoftEdgeDither = false };
            header.mValidating = true;

            BenchPlace place;
            place.mView = "seyda-neen";
            record.add(place);

            const std::string build = Rtx::sAssertsOn ? "a build with asserts, not one to quote" : "a release build";
            const std::string expected = "\nrun  " + build
                + ", layers on, not a figure to quote, not measured, every frame hashed, the weather turned\n"
                  "     1920x1080 from 960x540, upscale performance, filter off, jitter on, noise white-hash, "
                  "level bias -1.000, antilag on, history fix off, dual motion on, anti-firefly on, "
                  "shadow floor 0.0625, lamp candidates 4, indirect off, bounce rays 3, relief off, soft edge "
                  "dither off, water interleave on\n"
                  "     gamma 2.20, show albedo, exposure fixed at 1.500, variants off, hold 8.0 ms\n"
                  "     land 4.0 cells, viewing distance 7168, distant statics off, maps player, step 0.0625 s, "
                  "walks streamed, memory budget 512.0 MiB, host pages ";
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

            // A session nobody plays names its maps by the shipped patterns, so two machines trace one scene.
            RunRecord unplayed;
            unplayed.begin(SessionRequest{});
            unplayed.add(BenchPlace{});
            EXPECT_NE(unplayed.getReport().find(", maps shipped, "), std::string::npos) << unplayed.getReport();

            record.finish(request);
            std::ostringstream read;
            read << std::ifstream(request.mJson).rdbuf();

            const std::string json = read.str();
            constexpr std::string_view premises
                = R"("measures": false, "hashed": true, "turnsWeather": true, "maps": "player", "hugePageShare": )";
            constexpr std::string_view setup
                = R"(  "filter": false, "jitter": false, "gamma": 2.200, "show": "albedo", )"
                  R"("exposure": 1.5000, "exposureHeld": false, "variants": false, "holdMs": 8.000,)";
            constexpr std::string_view mirror = R"(  "landCells": 4.0, "viewingDistance": 7168.0, )"
                                                R"("distantStatics": false, "step": 0.0625, "settled": false, )"
                                                R"("memoryBudget": 536870912,)";
            EXPECT_NE(json.find(premises), std::string::npos) << json;
            EXPECT_NE(json.find(setup), std::string::npos) << json;
            EXPECT_NE(json.find(mirror), std::string::npos) << json;
            EXPECT_NE(
                json.find(R"("antilag": true, "historyFix": false, "dualMotion": true, )"
                          R"("antiFirefly": true, "shadowFloor": 0.0625, "lampCandidates": 4, "indirect": false, )"
                          R"("bounceRays": 3, "relief": false,)"),
                std::string::npos)
                << json;
            EXPECT_NE(json.find(Rtx::sAssertsOn ? R"("asserts": true)" : R"("asserts": false)"), std::string::npos);
        }

        /// **A record that cannot be written fails the run and keeps its report.** The close runs inside
        /// the engine's frame, so a throw from there would lose every measured place; a path nobody can
        /// write is one line under them and a status of 1.
        TEST(RtxRunRecordTest, aFileItCannotWriteFailsTheRunAndKeepsTheReport)
        {
            const std::filesystem::path nowhere = TestingOpenMW::currentTestDirPath() / "no-such-folder" / "deeper";
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

            const SessionResult result = record.conclude(nullptr);
            EXPECT_EQ(result.mExitStatus, ExitStatus::Failed);
            EXPECT_EQ(result.mReport.find(place), 0u) << result.mReport;
            EXPECT_NE(result.mReport.find("could not write " + Files::pathToUnicodeString(nowhere / "hashes.csv")),
                std::string::npos)
                << result.mReport;
            EXPECT_NE(result.mReport.find("could not write " + Files::pathToUnicodeString(nowhere / "record.json")),
                std::string::npos)
                << result.mReport;
            EXPECT_EQ(result.mReport.find("wrote"), std::string::npos) << result.mReport;
        }

        /// **A run that failed before its last stop is closed for the stops it reached**, so the record of
        /// the run before it does not stand at the path as this one's. A frame noted and never pictured is
        /// dropped rather than written as a hash of nothing, and the run fails.
        TEST(RtxRunRecordTest, anAbandonedRunWritesWhatItReachedAndFails)
        {
            const std::filesystem::path hashes = TestingOpenMW::currentTestDirPath() / "abandoned-hashes.csv";
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

            EXPECT_EQ(record.conclude(nullptr).mExitStatus, ExitStatus::Failed);
            EXPECT_EQ(record.getHashes().frameCount(), 1u) << "the frame whose picture never came";
            EXPECT_NE(record.getReport().find("wrote 1 frame hashes"), std::string::npos) << record.getReport();

            std::ostringstream read;
            read << std::ifstream(hashes).rdbuf();
            EXPECT_EQ(read.str().find("the run before"), std::string::npos) << read.str();
            EXPECT_NE(read.str().find("seyda-neen,1,"), std::string::npos) << read.str();
        }

        /// **A run that begins removes everything the last run wrote where it writes**: its hashes, JSON,
        /// each stop's capture, frame times, sheet, map tile, doll and mean, and the measured frames'
        /// pictures, `view-frame.png`, so a run that dies early leaves nothing another build wrote beside
        /// its pictures. What the run does not name stays: another view's pictures, and a file of the
        /// stop's name that is no frame's.
        TEST(RtxRunRecordTest, aRunThatBeginsRemovesTheLastRunsOutputs)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "begun";
            std::filesystem::create_directories(folder);

            Stop stop;
            stop.mName = "pier";
            stop.mActions.mCapture = Actions::Capture{ .mFile = folder / "pier.png" };
            stop.mActions.mFrameTimes = folder / "pier.txt";
            stop.mActions.mSheet = folder / "pier-textures.png";
            stop.mActions.mMapTile = folder / "pier-map.png";
            stop.mActions.mDoll = Actions::Doll{ .mWho = "someone", .mFile = folder / "pier-doll.png" };
            stop.mActions.mMean = Actions::Mean{ .mFile = folder / "mean.png", .mOf = 2 };

            SessionRequest request;
            request.mStops = { stop };
            request.mHashes = folder / "hashes.csv";
            request.mJson = folder / "begun.json";
            request.mPictures = folder;

            const std::vector<std::string> earlier{ "hashes.csv", "begun.json", "pier.png", "pier.txt",
                "pier-textures.png", "pier-map.png", "pier-doll.png", "mean.png", "pier-1.png", "pier-24.png" };
            const std::vector<std::string> kept{ "deck-1.png", "pier-notes.png", "pier-1.txt" };
            for (const std::vector<std::string>* names : { &earlier, &kept })
                for (const std::string& name : *names)
                    std::ofstream(folder / name) << "the run before\n";

            RunRecord record;
            record.begin(request);

            for (const std::string& name : earlier)
                EXPECT_FALSE(std::filesystem::exists(folder / name)) << "the last run's " << name << " stood";
            for (const std::string& name : kept)
                EXPECT_TRUE(std::filesystem::exists(folder / name)) << name << " is no output of this run";
        }
    }
}
