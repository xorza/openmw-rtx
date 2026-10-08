#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/noise.hpp>
#include <apps/rtxtool/run.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>

namespace RtxTool
{
    namespace
    {
        /// **An unfiltered picture at a held exposure warms over the air's decay** (`sAirFrames`),
        /// where a filtered one warms over four accumulator lengths (`sHistoryFrames`): the air keeps
        /// 0.9 of itself a frame, and the accumulator's `(31/32)^128 = 0.0172` is passed at
        /// `0.9^39 = 0.0164` and not at `0.9^38 = 0.0182`.
        TEST(RtxNoiseFrameTest, anUnfilteredPictureWarmsOverTheAirsDecay)
        {
            EXPECT_EQ(RtxTool::sHistoryFrames, 128u);
            EXPECT_EQ(RtxTool::sAirFrames, 39u);
        }

        /// **A strafed bar averages as many frames as the history could hold.** Native traces
        /// every shown pixel and is held to the sixteen of a still frame; quality traces 1280 by 720,
        /// `30 * 921600 / 2073600` = 13.3 samples a shown pixel, held to 13; ultra performance traces
        /// 640 by 360, 3.3, held to 3. And never nought, however short the history.
        ///
        /// **And the frame a leg judges holds what the leg says.** Standing, the warm-up is the stop's
        /// own 128 frames and its history 130, held to sixteen at native and at ultra performance to
        /// `130 * 230400 / 2073600` = 14.4, so 14; flown in, the flight's thirty frames; `--cut=N`, a warm-up of
        /// `N - 1` after the frame the cut resets, so `N + 1` frames of history: `--cut=1` at native
        /// holds 2 and is held to 2, at quality `2 * 921600 / 2073600` = 0.89, held to 1; `--cut=4`
        /// at native holds 5. A cut and a flight together are refused.
        TEST(RtxNoiseFrameTest, aStrafedBarAveragesAsManyFramesAsTheHistoryCouldHold)
        {
            const auto after = [](std::uint32_t frames, Rtx::Upscale mode) {
                return noiseBarFramesAfter(frames, Rtx::extentsFor(1920, 1080, mode));
            };

            EXPECT_EQ(after(30, Rtx::Upscale::Off), sNoiseBarFrames);
            EXPECT_EQ(after(30, Rtx::Upscale::Native), sNoiseBarFrames);
            EXPECT_EQ(after(30, Rtx::Upscale::Quality), 13u);
            EXPECT_EQ(after(30, Rtx::Upscale::UltraPerformance), 3u);
            EXPECT_EQ(after(8, Rtx::Upscale::Native), 8u);
            EXPECT_EQ(after(1, Rtx::Upscale::UltraPerformance), 1u);

            const auto taken = [](std::uint32_t cut, bool flies, Rtx::Upscale mode) {
                return noiseFrameFor(cut, flies, Rtx::extentsFor(1920, 1080, mode));
            };
            const NoiseFrame standing = taken(0, false, Rtx::Upscale::Native).value();
            EXPECT_FALSE(standing.mWarmup.has_value());
            EXPECT_EQ(standing.mBarFrames, sNoiseBarFrames);
            EXPECT_EQ(taken(0, false, Rtx::Upscale::UltraPerformance).value().mBarFrames, 14u);
            const NoiseFrame flown = taken(0, true, Rtx::Upscale::Quality).value();
            EXPECT_FALSE(flown.mWarmup.has_value());
            EXPECT_EQ(flown.mBarFrames, 13u);
            const NoiseFrame first = taken(1, false, Rtx::Upscale::Native).value();
            EXPECT_EQ(first.mWarmup, 0u);
            EXPECT_EQ(first.mBarFrames, 2u);
            EXPECT_EQ(taken(1, false, Rtx::Upscale::Quality).value().mBarFrames, 1u);
            const NoiseFrame fourth = taken(4, false, Rtx::Upscale::Native).value();
            EXPECT_EQ(fourth.mWarmup, 3u);
            EXPECT_EQ(fourth.mBarFrames, 5u);
            EXPECT_FALSE(taken(2, true, Rtx::Upscale::Native).isOk());
        }

        /// A place at the origin facing a point a hundred units north, level.
        Stop pier()
        {
            return Stop{ .mName = "pier",
                .mStand = { .mCell = "Seyda Neen", .mEye = osg::Vec3f(0, 0, 0), .mLook = osg::Vec3f(0, 100, 0) } };
        }

        NoiseAsk askAt(const std::span<const Stop> places)
        {
            return NoiseAsk{ .mPlaces = places,
                .mFolder = "out",
                .mPlayed = Rtx::ReconstructionRequest{},
                .mVersus = std::nullopt,
                .mExtents = Rtx::extentsFor(1920, 1080, Rtx::Upscale::Off),
                .mStep = 0.1f };
        }

        std::string refusal(const NoiseAsk& ask)
        {
            try
            {
                planNoise(ask);
            }
            catch (const std::runtime_error& error)
            {
                return error.what();
            }
            return "nothing was refused";
        }

        /// **A place standing is five pictures from 67 stops, each drawing samples of its own.** With
        /// the stride `S` = 1000003 and the bar's warm-up `128 - 39 = 89` frames shorter than the
        /// frame's:
        /// - the reference, 256 frames summed, unfiltered and jittered on white noise, at offset 0;
        /// - the bar, 16 frames summed (the frame's history of 130, held to 16), warmed over the air's
        ///   39, at `S + 89` = 1000092;
        /// - the bar's limit, 32 draws at `(2 + d) * S + 89`, the last `33 * S + 89` = 33000188;
        /// - the frame, one frame at the run's own reconstruction, at `34 * S` = 34000102;
        /// - its mean, 32 draws at `(35 + d) * S`, the last `66 * S` = 66000198.
        TEST(RtxNoisePlanTest, aPlaceStandingIsFivePicturesEachDrawnApart)
        {
            const std::array places{ pier() };
            const NoisePlan plan = planNoise(askAt(places));

            ASSERT_EQ(plan.mStops.size(), 67u);
            EXPECT_EQ(plan.mBarFrames, 16u);
            EXPECT_FALSE(plan.mOwnBar);
            EXPECT_FALSE(plan.mOwnReference);
            ASSERT_EQ(plan.mSides.size(), 1u);
            EXPECT_EQ(plan.mSides[0].mPlace, "pier");
            EXPECT_EQ(plan.mSides[0].mFrame, "pier");
            EXPECT_EQ(plan.mSides[0].mBar, "pier");
            EXPECT_EQ(plan.mSides[0].mReference, "pier");
            EXPECT_TRUE(plan.mVersusSides.empty());

            const Stop& truth = plan.mStops[0];
            EXPECT_EQ(truth.mName, "pier-reference");
            EXPECT_EQ(truth.mSchedule.mAccumulate, 256u);
            EXPECT_EQ(truth.mSchedule.mSpec.mRun.mFrames, 256u);
            EXPECT_EQ(truth.mSchedule.mSpec.mWarm.mFrames, 128u);
            EXPECT_EQ(truth.mSchedule.mSampleOffset, 0u);
            EXPECT_EQ(truth.mSchedule.mUpscale, Rtx::Upscale::Off);
            EXPECT_FALSE(truth.mSchedule.mExposure.has_value()) << "the reference ends on the exposure the rest hold";
            ASSERT_TRUE(truth.mSchedule.mReconstruction.has_value());
            EXPECT_FALSE(truth.mSchedule.mReconstruction->mDenoise);
            EXPECT_TRUE(truth.mSchedule.mReconstruction->mJitter);
            EXPECT_EQ(truth.mSchedule.mReconstruction->mSampling.mNoise, Rtx::NoiseSource::WhiteHash);
            EXPECT_EQ(truth.mSchedule.mReconstruction->mSampling.mLampCandidates, 0u);
            EXPECT_TRUE(truth.mActions.mDeepCapture);
            EXPECT_EQ(truth.mActions.mCapture, std::filesystem::path("out") / "pier-reference.png");

            const Stop& bar = plan.mStops[1];
            EXPECT_EQ(bar.mName, "pier-averaged");
            EXPECT_EQ(bar.mSchedule.mAccumulate, 16u);
            EXPECT_EQ(bar.mSchedule.mSpec.mWarm.mFrames, 39u);
            EXPECT_EQ(bar.mSchedule.mSampleOffset, 1000092u);
            EXPECT_EQ(bar.mSchedule.mReconstruction, Rtx::ReconstructionRequest{}.unfiltered());
            ASSERT_TRUE(bar.mSchedule.mExposure.has_value());
            EXPECT_TRUE(std::holds_alternative<Rtx::HeldExposure>(*bar.mSchedule.mExposure));

            const Stop& lastLimit = plan.mStops[33];
            EXPECT_EQ(lastLimit.mName, "pier-averaged-limit");
            EXPECT_EQ(lastLimit.mSchedule.mSampleOffset, 33000188u);
            EXPECT_TRUE(lastLimit.mActions.mCapture.empty());
            ASSERT_TRUE(lastLimit.mActions.mMean.has_value());
            EXPECT_EQ(lastLimit.mActions.mMean->mFile, std::filesystem::path("out") / "pier-averaged-limit.png");
            EXPECT_EQ(lastLimit.mActions.mMean->mOf, 32u);

            const Stop& frame = plan.mStops[34];
            EXPECT_EQ(frame.mName, "pier");
            EXPECT_EQ(frame.mSchedule.mSpec.mRun.mFrames, 1u);
            EXPECT_EQ(frame.mSchedule.mSpec.mWarm.mFrames, 128u);
            EXPECT_EQ(frame.mSchedule.mAccumulate, 0u);
            EXPECT_EQ(frame.mSchedule.mSampleOffset, 34000102u);
            EXPECT_EQ(frame.mSchedule.mReconstruction, Rtx::ReconstructionRequest{});
            EXPECT_FALSE(frame.mSchedule.mUpscale.has_value()) << "the frame upscales as the run does";
            EXPECT_FALSE(frame.mSchedule.mRoute.has_value());
            EXPECT_EQ(frame.mActions.mCapture, std::filesystem::path("out") / "pier.png");

            const Stop& lastMean = plan.mStops[66];
            EXPECT_EQ(lastMean.mName, "pier-mean");
            EXPECT_EQ(lastMean.mSchedule.mSampleOffset, 66000198u);
            EXPECT_EQ(lastMean.mActions.mMean->mFile, std::filesystem::path("out") / "pier-mean.png");
        }

        /// **The other side traces only what its switch can move**, its frame at the first side's
        /// draws: a filter switch moves the frame alone, 33 stops more; the jitter, the noise or the
        /// shadow floor move the unfiltered frames and so the bar, 66 more, and never the reference,
        /// since the truth sets each for itself.
        TEST(RtxNoisePlanTest, theOtherSideTracesOnlyWhatItsSwitchMoves)
        {
            const std::array places{ pier() };
            const auto against = [&](const Rtx::ReconstructionRequest& versus) {
                NoiseAsk ask = askAt(places);
                ask.mVersus = versus;
                return planNoise(ask);
            };

            Rtx::ReconstructionRequest filter;
            filter.mFilters.mAntiFirefly = true;
            const NoisePlan filtered = against(filter);
            EXPECT_EQ(filtered.mStops.size(), 67u + 33u);
            EXPECT_FALSE(filtered.mOwnBar);
            EXPECT_FALSE(filtered.mOwnReference);
            ASSERT_EQ(filtered.mVersusSides.size(), 1u);
            EXPECT_EQ(filtered.mVersusSides[0].mPlace, "pier");
            EXPECT_EQ(filtered.mVersusSides[0].mFrame, "pier-versus");
            EXPECT_EQ(filtered.mVersusSides[0].mBar, "pier");
            EXPECT_EQ(filtered.mVersusSides[0].mReference, "pier");
            const Stop& otherFrame = filtered.mStops[67];
            EXPECT_EQ(otherFrame.mName, "pier-versus");
            EXPECT_EQ(otherFrame.mSchedule.mSampleOffset, filtered.mStops[34].mSchedule.mSampleOffset)
                << "the other side's frame draws what the first side's drew";
            EXPECT_EQ(otherFrame.mSchedule.mReconstruction, filter);

            Rtx::ReconstructionRequest jittered;
            jittered.mJitter = true;
            const NoisePlan barred = against(jittered);
            EXPECT_EQ(barred.mStops.size(), 67u + 66u);
            EXPECT_TRUE(barred.mOwnBar);
            EXPECT_FALSE(barred.mOwnReference);
            EXPECT_EQ(barred.mVersusSides[0].mBar, "pier-versus");
            EXPECT_EQ(barred.mVersusSides[0].mReference, "pier");
            EXPECT_EQ(barred.mStops[67].mName, "pier-versus-averaged");

            Rtx::ReconstructionRequest floored;
            floored.mSampling.mShadowFloor = 0.5f;
            const NoisePlan truthful = against(floored);
            EXPECT_EQ(truthful.mStops.size(), 67u + 66u);
            EXPECT_TRUE(truthful.mOwnBar);
            EXPECT_FALSE(truthful.mOwnReference) << "the truth draws every source for its bit, at any floor";

            Rtx::ReconstructionRequest whiteNoise;
            whiteNoise.mSampling.mNoise = Rtx::NoiseSource::WhiteHash;
            const NoisePlan white = against(whiteNoise);
            EXPECT_TRUE(white.mOwnBar);
            EXPECT_FALSE(white.mOwnReference) << "the truth draws white noise whatever the side";
        }

        /// **A leg moves the frame alone.** `--cut=4` warms it 3 frames and its bar holds 5; a strafe
        /// flies it in over 30 frames along the approach its stand makes; and the lines it cannot
        /// draw are refused by what is wrong with them.
        TEST(RtxNoisePlanTest, aLegMovesTheFrameAloneAndWhatItCannotDrawIsRefused)
        {
            const std::array places{ pier() };

            NoiseAsk cut = askAt(places);
            cut.mCut = 4;
            const NoisePlan afterCut = planNoise(cut);
            EXPECT_EQ(afterCut.mBarFrames, 5u);
            EXPECT_EQ(afterCut.mStops[34].mSchedule.mSpec.mWarm.mFrames, 3u);
            EXPECT_EQ(afterCut.mStops[35].mSchedule.mSpec.mWarm.mFrames, 3u) << "the frame's mean draws the frame";
            EXPECT_EQ(afterCut.mStops[0].mSchedule.mSpec.mWarm.mFrames, 128u) << "the reference kept its own";

            NoiseAsk strafed = askAt(places);
            strafed.mStrafe = 150.0f;
            const NoisePlan flown = planNoise(strafed);
            const Stop& frame = flown.mStops[34];
            EXPECT_EQ(frame.mSchedule.mSpec.mRun.mFrames, sNoiseFlightFrames);
            EXPECT_TRUE(frame.mSchedule.mRoute.has_value());
            const Approach approach = places[0].mStand.approachFrom(150.0f, 0.0f, 0.1f, sNoiseFlightFrames);
            EXPECT_EQ(frame.mStand.mEye, approach.mFrom.mEye);
            EXPECT_EQ(flown.mStops[0].mStand.mEye, places[0].mStand.mEye) << "the reference stands where it was";

            NoiseAsk both = strafed;
            both.mCut = 2;
            EXPECT_EQ(refusal(both),
                "--cut takes the frame standing after a cut, and --strafe and --walk fly it in: name one");

            const std::array blind{ Stop{ .mName = "blind", .mStand = { .mCell = "Seyda Neen" } } };
            NoiseAsk noEye = askAt(blind);
            noEye.mStrafe = 150.0f;
            EXPECT_EQ(refusal(noEye), "--strafe and --walk need a place that names an eye, and blind names none");

            // The pier faces a point 100 units ahead: a walk from 100 behind it starts on it.
            NoiseAsk past = askAt(places);
            past.mWalk = -100.0f;
            EXPECT_EQ(refusal(past), "--walk=-100 starts past the point pier faces");
            past.mWalk = -99.0f;
            EXPECT_EQ(refusal(past), "nothing was refused");
        }
    }
}
