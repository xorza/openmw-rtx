#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <Base64.h>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>
#include <osg/Vec2i>

#include <apps/rtxtool/compare.hpp>
#include <apps/rtxtool/framed.hpp>
#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/options.hpp>
#include <apps/rtxtool/plans.hpp>
#include <apps/rtxtool/toolline.hpp>
#include <apps/rtxtool/verbs.hpp>
#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtxvulkan/createrenderer.hpp>
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
                    std::ofstream(file, std::ios::binary) << Base64::Base64::Encode("[Video]\ngamma = 1.25\n");
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
