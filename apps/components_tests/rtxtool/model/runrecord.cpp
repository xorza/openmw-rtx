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
#include <components/platform/heap.hpp>
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
        /// a failed check is a failure, but for a run that watches for findings, whose table judges it.
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

            // A tour's failed check is one of its findings, which the content may be known to meet.
            SessionRequest touring;
            touring.mWatching = SessionRequest::Watching{};
            RunRecord watched;
            watched.begin(touring);
            watched.checked(Check::TexturesReadable, false);
            EXPECT_EQ(watched.conclude(nullptr).mExitStatus, ExitStatus::Passed) << "the record judged a finding";
            EXPECT_EQ(watched.takeFailedChecks(), (std::vector<Check>{ Check::TexturesReadable }));

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
            request.mSetup.mCacheBudget = 256ull * 1024 * 1024;
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
                  "walks streamed, memory budget 512.0 MiB, cache budget 256.0 MiB, host pages ";
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

            // **The process's peak, resident and swapped together**: 1000 + 200 = 1200 MiB at the first stop over the
            // second's 900 + 100 = 1000, with the heap there 300 MiB taken and 250 in use, 50 free, of which the
            // five arenas keep 2, 30, 0, 10 and 8: the four keeping the most, 30, 10, 8 and 2.
            constexpr std::uint64_t sMiB = std::uint64_t{ 1 } << 20;
            record.noteHostMemory("first", { .mResident = 1000 * sMiB, .mNotResident = 200 * sMiB },
                Platform::HeapHeld{ .mTaken = 300 * sMiB,
                    .mInUse = 250 * sMiB,
                    .mArenas = { { .mTaken = 40 * sMiB, .mFree = 2 * sMiB },
                        { .mTaken = 100 * sMiB, .mFree = 30 * sMiB }, { .mTaken = 20 * sMiB, .mFree = 0 },
                        { .mTaken = 60 * sMiB, .mFree = 10 * sMiB }, { .mTaken = 80 * sMiB, .mFree = 8 * sMiB } } });
            record.noteHostMemory("second", { .mResident = 900 * sMiB, .mNotResident = 100 * sMiB }, std::nullopt);

            // **The device's peak by use**: balmora's 100 + 50 + 900 = 1050 MiB over seyda-neen's nought and vivec's
            // 100 + 50 + 850 = 1000; what is kept for the frame is no use.
            BenchPlace balmora;
            balmora.mView = "balmora";
            balmora.mMemory.mUses = Rtx::MemoryUses{
                .mFrame = 100 * sMiB, .mFrameReserve = 400 * sMiB, .mEssential = 50 * sMiB, .mContent = 900 * sMiB
            };
            BenchPlace vivec;
            vivec.mView = "vivec";
            vivec.mMemory.mUses = Rtx::MemoryUses{
                .mFrame = 100 * sMiB, .mFrameReserve = 4000 * sMiB, .mEssential = 50 * sMiB, .mContent = 850 * sMiB
            };
            // **The geometry's peak**, host and device together: vivec's 40 + 60 MiB over balmora's 30 + 50.
            balmora.mContent
                = Rtx::ContentMemory{ .mVertexEnd = 1'000'000, .mGeometryBytes = 30 * sMiB, .mReader = {} };
            balmora.mScene.mGeometryBytes = 50 * sMiB;
            vivec.mContent = Rtx::ContentMemory{ .mVertexEnd = 1'600'000,
                .mVerticesUsed = 1'500'000,
                .mIndexEnd = 3'000'000,
                .mIndicesUsed = 2'400'000,
                .mRestingVertices = 300'000,
                .mRestingIndices = 600'000,
                .mVertexHoles = 12,
                .mIndexHoles = 34,
                .mGeometryBytes = 40 * sMiB,
                .mReader = {},
                .mRing = {} };
            vivec.mScene.mGeometryBytes = 60 * sMiB;
            // **The ring, the reader and the game at their peaks**, and what only grows at the last stop: vivec's
            // 10 - 4 = 6 meshes standing nowhere over balmora's 3 - 3; balmora's reader room of 10 MiB over vivec's 8;
            // vivec's 40 cell stores over 30.
            constexpr std::uint64_t sVertices = 1'000'000;
            vivec.mContent.mRing.mByDistance[2] = Rtx::RingDistance{ .mPlacements = 20,
                .mPlacementsStanding = 9,
                .mMeshes = 10,
                .mMeshesStanding = 4,
                .mVertices = sVertices,
                .mVerticesStanding = 4 * sVertices / 10 };
            balmora.mContent.mRing.mByDistance[0]
                = Rtx::RingDistance{ .mPlacements = 3, .mPlacementsStanding = 3, .mMeshes = 3, .mMeshesStanding = 3 };
            balmora.mContent.mReader = Rtx::ReaderMemory{ .mLentBytes = 10 * sMiB, .mLentContentBytes = 5 * sMiB };
            vivec.mContent.mReader = Rtx::ReaderMemory{ .mSpareBytes = 8 * sMiB };
            balmora.mGame = GameHeld{ .mLoadedCells = 30, .mStarvedLooks = 2 };
            vivec.mGame = GameHeld{ .mLoadedCells = 40, .mPreloadedCells = 1, .mStarvedLooks = 7 };
            record.add(balmora);
            record.add(vivec);

            record.finish(request);
            EXPECT_NE(record.getReport().find("\nthe device held at most 1050.0 MiB by use, at balmora: frame 100.0 of "
                                              "400.0 kept   essential 50.0, growth 0.0   content 900.0\n"),
                std::string::npos)
                << record.getReport();
            EXPECT_NE(record.getReport().find("\nthe geometry reached at most 1.60 M vertices and 3.00 M indices, at "
                                              "vivec: 0.30 M and 0.60 M of them resting, 0.10 M and 0.60 M in 12 "
                                              "and 34 holes; 40.0 MiB on the host, 60.0 MiB on the device\n"),
                std::string::npos)
                << record.getReport();
            EXPECT_NE(
                record.getReport().find("\nthe ring held at most 6 meshes standing in no placement, 0.60 M "
                                        "vertices, at vivec\nthe reader held at most 10.0 MiB of room for 5.0 MiB "
                                        "of models, at balmora\nthe game held at most 40 cell stores loaded, at "
                                        "vivec\n7 looks for room in the caches found all in use\n"),
                std::string::npos)
                << record.getReport();
            unplayed.add(BenchPlace{});
            unplayed.finish(SessionRequest{});
            EXPECT_NE(unplayed.getReport().find("\n2 places"), std::string::npos) << unplayed.getReport();
            EXPECT_EQ(unplayed.getReport().find("the device held"), std::string::npos)
                << "a renderer that said nothing";
            EXPECT_NE(record.getReport().find(
                          "\nthe process held at most 1200.0 MiB of anonymous memory, 200.0 MiB of it "
                          "not resident, at first; there the heap took 300.0 MiB, 50.0 MiB of it free, in "
                          "5 arenas, the most free 30.0 of 100.0 MiB, 10.0 of 60.0 MiB, 8.0 of 80.0 MiB, "
                          "2.0 of 40.0 MiB\n"),
                std::string::npos)
                << record.getReport();
            std::ostringstream read;
            read << std::ifstream(request.mJson).rdbuf();

            const std::string json = read.str();
            constexpr std::string_view premises
                = R"("measures": false, "hashed": true, "turnsWeather": true, "maps": "player", "hugePageShare": )";
            constexpr std::string_view setup
                = R"("filter": false, "jitter": false, "gamma": 2.200, "show": "albedo", )"
                  R"("exposure": 1.5000, "exposureHeld": false, "variants": false, "holdMs": 8.000, )";
            constexpr std::string_view mirror = R"("landCells": 4.0, "viewingDistance": 7168.0, )"
                                                R"("distantStatics": false, "step": 0.0625, "settled": false, )"
                                                R"("memoryBudget": 536870912, "cacheBudget": 268435456}})";
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

        /// **A save adds what the record gained since the last, a line each, and writes nothing twice**: the header
        /// before the first place, each place once however often the run saves, and the findings' counts where they
        /// moved. What a tour does after every leg, so a save costs a place's worth and one that dies has said how far
        /// it came; the end adds what is left, and a record that measured nothing is still its header.
        TEST(RtxRunRecordTest, aSaveAddsWhatTheRecordGainedALineEach)
        {
            const std::filesystem::path folder = TestingOpenMW::currentTestDirPath() / "record-lines";
            std::filesystem::remove_all(folder);
            std::filesystem::create_directories(folder);
            const auto linesOf = [](const std::filesystem::path& path) {
                std::vector<std::string> lines;
                std::ifstream file(path);
                for (std::string line; std::getline(file, line);)
                    lines.push_back(line.substr(0, 18));
                return lines;
            };
            const auto placeAt = [](const std::string_view view) {
                BenchPlace place;
                place.mView = view;
                return place;
            };

            SessionRequest request;
            request.mJson = folder / "tour.jsonl";
            {
                RunRecord record;
                record.add(placeAt("leg-1"));
                record.count(FindingCounts{ .mFaults = 0, .mCosts = 2, .mExpected = 0 });
                record.save(request);
                record.save(request);
                record.add(placeAt("leg-2"));
                record.save(request);
                record.add(placeAt("leg-3"));
                record.count(FindingCounts{ .mFaults = 1, .mCosts = 2, .mExpected = 0 });
                record.finish(request);
                EXPECT_NE(
                    record.getReport().find("wrote " + Files::pathToUnicodeString(request.mJson)), std::string::npos)
                    << record.getReport();
            }
            EXPECT_EQ(linesOf(request.mJson),
                (std::vector<std::string>{ R"({"header": {"suite)", R"({"place": {"view":)", R"({"findings": {"fau)",
                    R"({"place": {"view":)", R"({"place": {"view":)", R"({"findings": {"fau)" }));

            request.mJson = folder / "nothing.jsonl";
            RunRecord nothing;
            nothing.finish(request);
            EXPECT_EQ(linesOf(request.mJson), (std::vector<std::string>{ R"({"header": {"suite)" }));
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
            stop.mActions.mMapTile = folder / "pier-map.png";
            stop.mActions.mDoll = Actions::Doll{ .mWho = "someone", .mFile = folder / "pier-doll.png" };
            stop.mActions.mMean = Actions::Mean{ .mFile = folder / "mean.png", .mOf = 2 };

            SessionRequest request;
            request.mStops = { stop };
            request.mHashes = folder / "hashes.csv";
            request.mJson = folder / "begun.json";
            request.mPictures = folder;

            const std::vector<std::string> earlier{ "hashes.csv", "begun.json", "pier.png", "pier.txt", "pier-map.png",
                "pier-doll.png", "mean.png", "pier-1.png", "pier-24.png" };
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
