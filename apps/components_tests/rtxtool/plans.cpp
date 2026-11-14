#include <algorithm>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <Base64.h>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <osg/Vec2i>

#include <apps/components_tests/rtx/support/refusal.hpp>
#include <apps/rtxtool/compare.hpp>
#include <apps/rtxtool/framed.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/tour.hpp>
#include <apps/rtxtool/options.hpp>
#include <apps/rtxtool/plans.hpp>
#include <apps/rtxtool/toolline.hpp>
#include <apps/rtxtool/verbs.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/createrenderer.hpp>
#include <components/settings/values.hpp>
#include <components/testing/util.hpp>

namespace RtxTool
{
    namespace
    {
        namespace bpo = boost::program_options;

        /// A command as `dispatch` hands one over, for `arguments` under `verb`: the line read against the harness's
        /// own description with no configuration file. The frame is the test's, as `frameFrom` would have framed it.
        class RtxPlanTest : public testing::Test
        {
        protected:
            RtxPlanTest()
                : mShipped([] {
                    const std::filesystem::path file = TestingOpenMW::currentTestDirPath() / "plan-shipped.bin";
                    std::ofstream(file, std::ios::binary) << Base64::Base64::Encode(
                        "[Video]\ngamma = 1.25\n[Cells]\npreload distance = 1000\npreload num threads = 1\n"
                        "preload cell cache min = 12\npreload cell cache max = 20\n[Physics]\nasync num threads = 1\n"
                        "[RTX]\nupscale = balanced\nupscaler = auto\n");
                    return ShippedDefaults(file);
                }())
            {
            }

            /// Plans `arguments` under `verb` with `plan` over `framed`, keeping the line for as long as the command
            /// it is read into is asked.
            template <class Plan>
            auto planOf(const Verb verb, const std::vector<std::string>& arguments, Plan plan, Framed framed = Framed{})
            {
                bpo::variables_map variables;
                bpo::store(bpo::command_line_parser(arguments).options(mOptions.mDescription).run(), variables);
                bpo::notify(variables);
                mLine = mOptions.read(variables, verb);
                return plan(Command{ mLine, mConfig, mOwnDirectory, Rtx::shippedVulkanShaders(), verb, mShipped },
                    std::move(framed));
            }

            const ToolOptions mOptions = makeOptions(Rtx::ValidationLevel::Off);
            Files::ConfigurationManager mConfig;
            const std::filesystem::path mOwnDirectory = TestingOpenMW::currentTestDirPath() / "own";
            const ShippedDefaults mShipped;
            ToolLine mLine;
        };

        /// **`check` holds the queue 8 ms behind the host unless its line names a hold**, which a test reaches as it
        /// reaches the verb's frame, and its frame is shown at the window's size in pixels. Every check a stop can
        /// answer is asked of it, and the first place writes every picture a stop can, into `--out`.
        TEST_F(RtxPlanTest, checkHoldsTheQueueAndAsksWhatEachPlaceCanAnswer)
        {
            const std::filesystem::path out = TestingOpenMW::currentTestDirPath() / "check";
            Framed framed;
            framed.mWindow.mWidth = sCheckFrame.mWidth;
            framed.mWindow.mHeight = sCheckFrame.mHeight;
            const HostedRun held = planOf(Verb::Check,
                { "--views=seyda-neen-ship", "--out=" + Files::pathToUnicodeString(out) }, planCheck, framed);
            const SessionRequest& request = held.mRequest;
            EXPECT_EQ(request.mSetup.mRun.mProfile.mStressOverlapMs, sCheckHoldMs);
            EXPECT_EQ(request.mSetup.mWindowPixels, osg::Vec2i(957, 539));
            EXPECT_EQ(held.mWindow.mWidth, 957u);
            EXPECT_EQ(held.mWindow.mHeight, 539u);
            ASSERT_EQ(request.mStops.size(), 1u);

            const Stop& stop = request.mStops.front();
            EXPECT_TRUE(stop.mSchedule.mFrozen) << "a still place, where `check` freezes the world";
            ASSERT_TRUE(stop.mActions.mCapture.has_value());
            EXPECT_EQ(stop.mActions.mCapture->mFile, out / "seyda-neen-ship.png");
            EXPECT_EQ(stop.mActions.mMapTile, out / "seyda-neen-ship-map.png");
            ASSERT_TRUE(stop.mActions.mDoll.has_value());
            EXPECT_EQ(stop.mActions.mDoll->mFile, out / "seyda-neen-ship-doll.png");

            std::vector<Check> asked;
            for (const Check check : everyCheck())
                if (canAsk(check, stop, request.mSetup.mRun.mProfile))
                    asked.push_back(check);
            EXPECT_EQ(stop.mActions.mChecks, asked);
            EXPECT_FALSE(asked.empty());

            const HostedRun unheld = planOf(Verb::Check,
                { "--views=seyda-neen-ship", "--hold=false", "--out=" + Files::pathToUnicodeString(out) }, planCheck);
            EXPECT_EQ(unheld.mRequest.mSetup.mRun.mProfile.mStressOverlapMs, 0.0) << "the line's hold is the line's";
        }

        /// **A shot's pictures are held to where they came from**: the frame by its hashes where an upscaler
        /// reconstructed it, by its picture where the wavelet composed it and nothing upscaled it; the tile denoised
        /// and the sheet exact. Each is named after its place under `--out`, which its hashes go beside.
        TEST_F(RtxPlanTest, aShotsPicturesAreHeldToWhereTheyCameFrom)
        {
            const std::filesystem::path out = TestingOpenMW::currentTestDirPath() / "shot";
            const std::vector<std::string> line{ "--views=seyda-neen-ship", "--map", "--textures",
                "--out=" + Files::pathToUnicodeString(out) };

            Framed framed;
            framed.mSetup.mRun.mProfile.mUpscale = Rtx::Upscale::Quality;
            framed.mSetup.mRun.mProfile.mReconstruction.mDenoise = true;
            const ShotPlan upscaled = planOf(Verb::Shot, line, planShot, framed);
            EXPECT_EQ(upscaled.mOut, out);
            ASSERT_EQ(upscaled.mPictures.size(), 3u);
            EXPECT_EQ(upscaled.mPictures[0].mFile, "seyda-neen-ship.png");
            EXPECT_EQ(upscaled.mPictures[0].mRule, PictureRule::Hashed);
            EXPECT_EQ(upscaled.mPictures[1].mFile, "seyda-neen-ship-map.png");
            EXPECT_EQ(upscaled.mPictures[1].mRule, PictureRule::Denoised);
            EXPECT_EQ(upscaled.mPictures[2].mFile, "seyda-neen-ship-textures.png");
            EXPECT_EQ(upscaled.mPictures[2].mRule, PictureRule::Exact);
            EXPECT_EQ(upscaled.mRun.mRequest.mHashes, out / sRunHashes);
            EXPECT_EQ(upscaled.mRun.mRequest.mAgainst, std::filesystem::path());

            Framed native;
            native.mSetup.mRun.mProfile.mUpscale = Rtx::Upscale::Off;
            native.mSetup.mRun.mProfile.mReconstruction.mDenoise = true;
            const ShotPlan composed = planOf(Verb::Shot, line, planShot, native);
            EXPECT_EQ(composed.mPictures[0].mRule, PictureRule::Denoised) << "the wavelet composed it";

            native.mSetup.mRun.mProfile.mReconstruction.mDenoise = false;
            EXPECT_EQ(planOf(Verb::Shot, line, planShot, native).mPictures[0].mRule, PictureRule::Hashed)
                << "nothing composed it past the trace";
        }

        /// **A bench writes and hashes nothing unless asked**, and refuses pictures with nowhere to write them.
        TEST_F(RtxPlanTest, aBenchWritesAndHashesNothingUnlessAsked)
        {
            const HostedRun plain = planOf(Verb::Bench, { "--views=seyda-neen-ship" }, planBench);
            ASSERT_EQ(plain.mRequest.mStops.size(), 1u);
            EXPECT_FALSE(plain.mRequest.mStops.front().mActions.mHash);
            EXPECT_EQ(plain.mRequest.mHashes, std::filesystem::path());
            EXPECT_EQ(plain.mRequest.mPictures, std::filesystem::path());
            EXPECT_TRUE(plain.mRequest.mMeasures);

            const std::filesystem::path out = TestingOpenMW::currentTestDirPath() / "bench";
            const HostedRun kept = planOf(Verb::Bench,
                { "--views=seyda-neen-ship", "--pictures", "--out=" + Files::pathToUnicodeString(out) }, planBench);
            EXPECT_TRUE(kept.mRequest.mStops.front().mActions.mHash);
            EXPECT_EQ(kept.mRequest.mHashes, out / sRunHashes);
            EXPECT_EQ(kept.mRequest.mPictures, out);

            try
            {
                planOf(Verb::Bench, { "--views=seyda-neen-ship", "--pictures" }, planBench);
                ADD_FAILURE() << "pictures with nowhere to write them were planned";
            }
            catch (const std::runtime_error& refused)
            {
                EXPECT_STREQ(refused.what(), "--pictures writes into --out, and no --out was named");
            }
        }

        /// **A window runs on the wall, played, until it is closed** or `--frames` have been drawn.
        TEST_F(RtxPlanTest, aWindowRunsOnTheWallUntilItIsClosed)
        {
            const HostedRun open = planOf(Verb::View, {}, planView);
            EXPECT_FALSE(open.mRequest.mSetup.mHeadless) << "a `Framed` is headless until a verb says otherwise";
            EXPECT_EQ(open.mRequest.mStep, std::nullopt);
            EXPECT_TRUE(open.mRequest.mPlayed);
            EXPECT_FALSE(open.mRequest.mQuitAtEnd);
            ASSERT_EQ(open.mRequest.mStops.size(), 1u);
            EXPECT_EQ(open.mRequest.mStops.front().mName, "seyda-neen-ship") << "where the game starts";
            EXPECT_EQ(open.mRequest.mStops.front().mSchedule.mSpec.mRun.mFrames, BenchSpan::sUntilClosed);

            const HostedRun counted = planOf(Verb::View, { "--frames=30" }, planView);
            EXPECT_TRUE(counted.mRequest.mQuitAtEnd);
            EXPECT_EQ(counted.mRequest.mStops.front().mSchedule.mSpec.mRun.mFrames, 30u);
        }

        /// **A tour plans its legs where the world stands, from the map the world gives, and goes on past a leg
        /// that fails**: two islands a sea apart, a room of the content's own and one `[interiors]` names, flown from
        /// `--from` on under the tour's row, each leg a track the session follows. `--plan` flies nothing.
        TEST_F(RtxPlanTest, aTourPlansWhereTheWorldStandsAndGoesOnPastAFailedLeg)
        {
            const auto map = [] {
                WorldMap made;
                made.mLand = { LandCell{ .mCell = osg::Vec2i(0, 0) }, LandCell{ .mCell = osg::Vec2i(10, 0) } };
                made.mFiles = { "Morrowind.esm" };
                made.mRooms = { Room{ .mName = "Arrille's Tradehouse", .mFile = 0, .mReferences = 50, .mEye = {} },
                    Room{ .mName = "Balmora, Guild of Mages", .mFile = 0, .mReferences = 1, .mEye = {} } };
                return made;
            };

            Framed framed;
            framed.mWindow.mWidth = sTourFrame.mWidth;
            framed.mWindow.mHeight = sTourFrame.mHeight;
            const HostedRun run = planOf(
                Verb::Tour, {},
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); }, framed);
            const SessionRequest& request = run.mRequest;
            EXPECT_TRUE(request.mStops.empty()) << "nothing to fly until the world stands";
            EXPECT_EQ(request.mWhenAStopFails, WhenAStopFails::GoesOn);
            EXPECT_EQ(request.mStep, sStepSeconds);
            EXPECT_EQ(request.mSetup.mWindowPixels, osg::Vec2i(1024, 768)) << "the tour's own frame";

            // A player's preloading at the flight's speed: 12000 over a running player's 337.5 is 35.6 times, so the
            // shipped 1000 units of warning become 35,556 and its one thread 36, held to the ring's 16 cells and to
            // what the processor spares.
            ASSERT_TRUE(run.mPreload.has_value());
            EXPECT_FLOAT_EQ(run.mPreload->mDistance, 1000.0f * 12000.0f / 337.5f);
            EXPECT_EQ(run.mPreload->mThreads,
                std::min(16, std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1)));
            EXPECT_EQ(run.mPreload->mCacheMin, 12) << "the shipped least, so what expired makes room";
            EXPECT_EQ(run.mPreload->mCacheMax, 50)
                << "the grid and its ring, 25, around where the player is and will be";
            EXPECT_EQ(run.mPhysicsThreads, 1) << "the game's threaded physics";
            EXPECT_TRUE(request.mHud) << "the interface over the frame, as a player sees it";
            EXPECT_TRUE(request.mSetup.mInterface);
            EXPECT_EQ(mLine.mFrame.mSize.mWidth, 1024u) << "read off a line that names no size";
            EXPECT_EQ(mLine.mFrame.mSize.mHeight, 768u);
            EXPECT_TRUE(request.mMeasures);
            ASSERT_TRUE(request.mPlanWhereTheWorldStands);

            // Two land legs, the share's one room and the room the suite names, and the first leg again.
            const std::vector<Stop> stops = request.mPlanWhereTheWorldStands(30.0f);
            std::vector<std::string> names;
            for (const Stop& stop : stops)
                names.push_back(stop.mName);
            EXPECT_EQ(names,
                (std::vector<std::string>{ "leg-1-land-0,1", "leg-2-land-10,1", "leg-3-room-Arrille's Tradehouse",
                    "leg-4-room-Balmora, Guild of Mages", "leg-5-closing-0,1" }));
            for (const Stop& stop : stops)
            {
                EXPECT_TRUE(stop.mSchedule.mTrack.has_value()) << stop.mName << ": the tour's row follows tracks";
                EXPECT_FALSE(stop.mSchedule.mFrozen) << stop.mName;
                std::vector<Check> asked;
                for (const Check check : everyCheck())
                    if (canAsk(check, stop, framed.mSetup.mRun.mProfile))
                        asked.push_back(check);
                EXPECT_EQ(stop.mActions.mChecks, asked) << stop.mName << ": every claim a leg can answer";
            }
            EXPECT_EQ(stops.front().mActions.mCovers, std::vector<osg::Vec2i>{ osg::Vec2i(0, 0) });
            ASSERT_TRUE(request.mWatching.has_value());
            EXPECT_TRUE(request.mWatching->mFailOn.empty());

            const HostedRun failing = planOf(Verb::Tour, { "--fail-on=hitch,growth" },
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            EXPECT_EQ(failing.mRequest.mWatching->mFailOn,
                (std::vector<FindingKind>{ FindingKind::Hitch, FindingKind::Growth }));
            EXPECT_EQ(Rtx::Testing::refusalOf([&] {
                planOf(Verb::Tour, { "--fail-on=stall" },
                    [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            }),
                "--fail-on names no kind of finding called \"stall\"");

            const HostedRun later = planOf(Verb::Tour, { "--from=4" },
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            EXPECT_EQ(
                later.mRequest.mPlanWhereTheWorldStands(30.0f).front().mName, "leg-4-room-Balmora, Guild of Mages");

            const HostedRun past = planOf(Verb::Tour, { "--from=6" },
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { past.mRequest.mPlanWhereTheWorldStands(30.0f); }),
                "--from=6 is past the tour's last leg, 5");

            // Held to the step, a tour's frames are limited to its rate; a pace or a leg the line cannot mean is
            // refused.
            const HostedRun paced = planOf(Verb::Tour, { "--pace=real" },
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            EXPECT_TRUE(paced.mRequest.mWatching->mPaced);
            EXPECT_EQ(paced.mWindow.mFramerateLimit, 60.0f);
            EXPECT_FALSE(request.mWatching->mPaced);
            EXPECT_EQ(run.mWindow.mFramerateLimit, 0.0f) << "as fast as the card draws";
            EXPECT_EQ(Rtx::Testing::refusalOf([&] {
                planOf(Verb::Tour, { "--pace=walk" },
                    [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            }),
                "--pace=walk is neither card nor real");
            EXPECT_EQ(Rtx::Testing::refusalOf([&] {
                planOf(Verb::Tour, { "--from=0" },
                    [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            }),
                "--from=0 names no leg: a tour's legs are counted from one");

            const HostedRun planned = planOf(Verb::Tour, { "--plan" },
                [&](const Command& command, Framed f) { return planTourRun(command, std::move(f), map); });
            EXPECT_TRUE(planned.mRequest.mPlanWhereTheWorldStands(30.0f).empty()) << "--plan flies nothing";
            EXPECT_TRUE(planned.mRequest.mPlansOnly);
            EXPECT_FALSE(request.mPlansOnly);
        }

        /// **A tour sees ten cells and upscales as the game ships upscaling**, where a measured run takes the harness's
        /// own quality and FSR: a tour flies what a player plays.
        TEST_F(RtxPlanTest, aTourSeesTenCellsAndUpscalesAsTheGameShips)
        {
            MWRender::RtxSettingValues measured;
            measured.mUpscale = Rtx::Upscale::Quality;
            measured.mUpscaler = Rtx::UpscalerChoice::Fsr;
            measured.mDistantLandCells = 3.0f;
            measured.mViewingDistance = 7168.0f;
            const MWRender::RtxSettingValues toured = touredSettings(mShipped, measured);
            EXPECT_EQ(toured.mUpscale, Rtx::Upscale::Balanced);
            EXPECT_EQ(toured.mUpscaler, Rtx::UpscalerChoice::Auto);
            EXPECT_EQ(toured.mDistantLandCells, 10.0f);
            EXPECT_EQ(toured.mViewingDistance, 81920.0f) << "ten cells of 8192";
        }

        /// **An empty defaults file is read as one that names nothing**, and a setting asked of it is refused by name.
        TEST(RtxShippedDefaultsTest, anEmptyFileNamesNothing)
        {
            const std::filesystem::path file = TestingOpenMW::currentTestDirPath() / "empty-shipped.bin";
            std::ofstream(file, std::ios::binary).flush();
            const ShippedDefaults empty(file);
            EXPECT_EQ(Rtx::Testing::refusalOf([&] { empty.get(Settings::video().mGamma); }),
                "defaults.bin names no [Video] gamma");
        }

        /// **A scene walks every place twice**, and digests it unless `--find` searches it instead.
        TEST_F(RtxPlanTest, aSceneWalksTwiceAndDigestsUnlessItSearches)
        {
            const HostedRun digested = planOf(Verb::Scene, { "--views=seyda-neen-ship" }, planScene);
            ASSERT_EQ(digested.mRequest.mStops.size(), 1u);
            const Actions& actions = digested.mRequest.mStops.front().mActions;
            EXPECT_TRUE(actions.mWalkTwice);
            EXPECT_TRUE(actions.mDigest);
            EXPECT_EQ(digested.mRequest.mStops.front().mSchedule.mSpec.mRun.mFrames, 1u);

            const HostedRun searched = planOf(Verb::Scene, { "--views=seyda-neen-ship", "--find=fargoth" }, planScene);
            EXPECT_FALSE(searched.mRequest.mStops.front().mActions.mDigest);
            EXPECT_EQ(searched.mRequest.mStops.front().mActions.mFind, "fargoth");
        }
    }
}
