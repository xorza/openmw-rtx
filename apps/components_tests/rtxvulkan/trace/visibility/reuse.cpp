#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <apps/components_tests/rtxvulkan/trace/visibility/fixture.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/material.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/bouncereuse.h>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/visibility.h>

namespace Rtx::Testing
{
    namespace
    {
        constexpr std::uint32_t sSize = 32;

        /// A corner in the sun: a floor at z = -100 and a wall across the view at y = 300, both
        /// four hundred units out from the middle, under a sky that lights. A bounce off the floor
        /// meets the wall or escapes past its top to the sky, and one off the wall meets the floor
        /// or the sky, so both kinds of sample are in every frame.
        SceneDesc makeCorner()
        {
            SceneDesc scene;
            addQuad(scene, sheetAt(400.0f, -100.0f));
            addQuad(scene, uprightQuadAt(400.0f, 300.0f));
            return scene;
        }

        Shaders::VisibilityConstants cornerCamera(std::uint32_t size = sSize)
        {
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -200.0f, 50.0f), osg::Vec3f(0.0f, 300.0f, -100.0f), 60.0f, size, size, 10000.0f);
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.3f, -0.5f, 0.8f), osg::Vec3f(3.0f, 3.0f, 3.0f));
            camera.mSkyHorizon = osg::Vec3f(0.4f, 0.5f, 0.6f);
            camera.mSkyZenith = osg::Vec3f(0.2f, 0.3f, 0.6f);
            camera.mAmbientFromSky = 1.0f;
            camera.mFrame = 7;
            return camera;
        }

        /// The luminance of every pixel of a channel read back.
        std::vector<float> luminanceOf(const std::vector<float>& read)
        {
            std::vector<float> luminance(read.size() / 4);
            for (std::size_t pixel = 0; pixel < luminance.size(); ++pixel)
                luminance[pixel] = osg::Vec3f(read[pixel * 4], read[pixel * 4 + 1], read[pixel * 4 + 2])
                    * Shaders::LUMINANCE_WEIGHTS;
            return luminance;
        }

        class RtxBounceReuseTest : public RtxVisibilityTest
        {
        protected:
            /// The bounce's luminance at every pixel of the corner, one vector a frame, over a run of
            /// `frames` from the sampler's frame `first` on with the history let build from nothing,
            /// past the first `skipped`.
            std::vector<std::vector<float>> bounceRun(BounceReuseRule reuse, std::uint32_t size, std::uint32_t frames,
                std::uint32_t skipped, std::uint32_t first)
            {
                std::vector<std::vector<float>> kept;
                std::vector<float> read;
                std::uint32_t at = 0;
                shoot(makeCorner(), {}, cornerCamera(size), size,
                    Shot{ .mFrames = frames,
                        .mAverage = false,
                        .mFirstFrame = first,
                        .mBounceReuse = reuse,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame&) {
                            if (at++ < skipped)
                                return;
                            mRenderer.readChannel(Channel::Indirect, read);
                            kept.push_back(luminanceOf(read));
                        } });
                return kept;
            }
        };

        /// Every pixel's mean over a run's frames.
        std::vector<float> meanOf(const std::vector<std::vector<float>>& frames)
        {
            std::vector<float> mean(frames.front().size());
            for (const std::vector<float>& frame : frames)
                for (std::size_t pixel = 0; pixel < mean.size(); ++pixel)
                    mean[pixel] += frame[pixel] / static_cast<float>(frames.size());
            return mean;
        }

        /// The mean over every pixel.
        float wholeOf(const std::vector<float>& pixels)
        {
            float sum = 0.0f;
            for (const float value : pixels)
                sum += value;
            return sum / static_cast<float>(pixels.size());
        }

        /// How far one frame stands from `truth`: the root mean square over its pixels, and over the
        /// frames of the run.
        float errorOf(const std::vector<std::vector<float>>& frames, const std::vector<float>& truth)
        {
            double sum = 0.0;
            for (const std::vector<float>& frame : frames)
                for (std::size_t pixel = 0; pixel < truth.size(); ++pixel)
                    sum += double(frame[pixel] - truth[pixel]) * double(frame[pixel] - truth[pixel]);
            return static_cast<float>(std::sqrt(sum / double(frames.size() * truth.size())));
        }

        /// **A pixel's own bounce taken through the reservoirs is the bounce the trace shades**, to
        /// what a reservoir stores. Both channels, at every pixel of a corner where the floor's
        /// bounces meet the wall and the sky, from the same draw.
        ///
        /// The bound, against the brightest channel `m` of the pixel's bounce: the light is stored as
        /// `RGB9E5`, nine bits under the brightest channel's exponent, so each channel moves by at
        /// most half a step, `2^-9 m`, and so does the transmittance it is put back under, which is
        /// one here and exact; the diffuse share is worked out again at the resolve from the stored
        /// direction, a rounding of the float's own. `2^-8 m` covers the light's half step twice over.
        TEST_F(RtxBounceReuseTest, theOwnReuseShadesTheTracesOwnBounce)
        {
            const SceneDesc scene = makeCorner();
            const Shaders::VisibilityConstants camera = cornerCamera();

            std::vector<float> plain;
            std::vector<float> plainFill;
            ASSERT_EQ(
                shoot(scene, {}, camera, sSize, Shot{ .mBounceReuse = BounceReuseRule::Off }).mHits, sSize * sSize);
            mRenderer.readChannel(Channel::Indirect, plain);
            mRenderer.readChannel(Channel::Fill, plainFill);

            std::vector<float> own;
            std::vector<float> ownFill;
            ASSERT_EQ(
                shoot(scene, {}, camera, sSize, Shot{ .mBounceReuse = BounceReuseRule::Own }).mHits, sSize * sSize);
            mRenderer.readChannel(Channel::Indirect, own);
            mRenderer.readChannel(Channel::Fill, ownFill);

            ASSERT_EQ(own.size(), plain.size());
            EXPECT_NE(own, plain) << "bit for bit the trace's own: the reservoirs were never read";
            ASSERT_EQ(ownFill.size(), plainFill.size());

            std::size_t lit = 0;
            for (std::size_t pixel = 0; pixel < own.size() / 4; ++pixel)
            {
                const float* const was = &plain[pixel * 4];
                const float brightest = std::max({ was[0], was[1], was[2] });
                const float bound = brightest / 256.0f + 1e-7f;
                lit += brightest > 0.0f ? 1 : 0;

                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    EXPECT_NEAR(own[pixel * 4 + channel], was[channel], bound)
                        << "pixel " << pixel << ", channel " << channel;
                    EXPECT_NEAR(ownFill[pixel * 4 + channel], plainFill[pixel * 4 + channel], bound)
                        << "the fill at pixel " << pixel << ", channel " << channel;
                }
            }

            // Most pixels find something: a bounce off the floor or the wall that brought nothing
            // back is a ray the sky behind the corner did not reach, which few are.
            EXPECT_GT(lit, std::size_t{ sSize } * sSize / 2) << "the corner bounced too little light to compare";
        }

        /// **The reuse keeps what a frame averages to and takes noise off each frame.** Every frame of
        /// a run with the reuse stands as an unbiased estimate of the bounce, the merge's weights
        /// being what makes it one, so the run averages to what the plain bounce converges to; and a
        /// frame stands nearer it the more each pixel reuses, its own past and then its neighbours'.
        ///
        /// The truth is 256 plain frames averaged, over a corner 128 pixels square. Each run is 64
        /// frames from a cut, of which the last 40 are read, so every reservoir has filled to its
        /// cap. **The mean is held to two per cent**: the reuse's frames are correlated, which widens
        /// what their mean wanders by. **The error of one frame falls by a fifth at least** with the
        /// temporal half and further with the spatial one, which is what the reuse is for.
        TEST_F(RtxBounceReuseTest, theReuseKeepsTheMeanAndTakesNoiseOffEachFrame)
        {
            const std::vector<float> truth = meanOf(bounceRun(BounceReuseRule::Off, 128, 256, 0, 10000));
            const float whole = wholeOf(truth);
            ASSERT_GT(whole, 0.0f);

            float errors[3]{};
            const BounceReuseRule reuses[3]{ BounceReuseRule::Own, BounceReuseRule::Temporal,
                BounceReuseRule::Spatiotemporal };
            for (std::size_t at = 0; at < 3; ++at)
            {
                const std::vector<std::vector<float>> run = bounceRun(reuses[at], 128, 64, 24, 20000);
                EXPECT_NEAR(wholeOf(meanOf(run)) / whole, 1.0f, 0.02f) << sBounceReuseRuleNames.name(reuses[at]);
                errors[at] = errorOf(run, truth);
            }

            EXPECT_LT(errors[1], 0.8f * errors[0])
                << "the temporal reuse took little noise off a frame: " << errors[1] << " against " << errors[0];
            EXPECT_LT(errors[2], errors[1]) << "the neighbours took no noise off what the past left";
        }

        /// **No light reaches the dark side of a wall of no thickness.** A floor in a room with no sky
        /// and no fill, cut in two by a wall three hundred units high, two-sided as the content's
        /// sheets are, with a lamp on the one side below its top: nothing on the other side is lit,
        /// so its bounce is nought exactly, and stays so however much the lit side next to it lends.
        ///
        /// **Seen from above**, so the two sides are neighbours on the screen and the spatial reuse
        /// reads across the wall. What it reads there is a sample on the lit face, which is the same
        /// point as the dark face behind it: a ray from the dark side reaches it, and only its side
        /// keeps it out (`shiftJacobian`). The dark pixels are the ones the plain bounce never lights
        /// in a run, and the test needs some of them within the neighbours' disc of a lit one.
        TEST_F(RtxBounceReuseTest, noLightReachesTheDarkSideOfAWallOfNoThickness)
        {
            constexpr std::uint32_t size = 64;
            SceneDesc scene;
            addQuad(scene, sheetAt(400.0f, 0.0f));
            const Index twoSided
                = scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f(0.5f, 0.5f, 0.5f), .mTwoSided = true });
            const std::array<osg::Vec3f, 4> wall{ osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 400.0f, 0.0f),
                osg::Vec3f(0.0f, 400.0f, 300.0f), osg::Vec3f(0.0f, -400.0f, 300.0f) };
            addQuad(scene, wall, twoSided);
            scene.addLight(Light{
                .mPosition = osg::Vec3f(150.0f, 0.0f, 50.0f),
                .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                .mReach = 1000.0f,
            });

            Shaders::VisibilityConstants camera
                = makeCamera(osg::Vec3f(0.0f, 0.0f, 900.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mAmbientFromSky = 0.0f;

            const auto lightest = [&](BounceReuseRule reuse) {
                std::vector<float> most(std::size_t{ size } * size);
                std::vector<float> read;
                std::uint32_t at = 0;
                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = 48,
                        .mAverage = false,
                        .mFirstFrame = 500,
                        .mBounceReuse = reuse,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame&) {
                            if (at++ < 16)
                                return;
                            mRenderer.readChannel(Channel::Indirect, read);
                            for (std::size_t pixel = 0; pixel < most.size(); ++pixel)
                                most[pixel] = std::max(
                                    { most[pixel], read[pixel * 4], read[pixel * 4 + 1], read[pixel * 4 + 2] });
                        } });
                return most;
            };

            const std::vector<float> plain = lightest(BounceReuseRule::Off);
            const std::vector<float> reused = lightest(BounceReuseRule::Spatiotemporal);

            // The wall stands on the middle column, and the lamp's half is the one the plain bounce
            // lights: the other is the dark side, past a column either side of the wall.
            const auto litIn = [&](std::uint32_t from, std::uint32_t to) {
                std::size_t lit = 0;
                for (std::uint32_t y = 0; y < size; ++y)
                    for (std::uint32_t x = from; x < to; ++x)
                        lit += plain[std::size_t{ y } * size + x] > 0.0f ? 1 : 0;
                return lit;
            };
            const bool darkLeft = litIn(0, size / 2) < litIn(size / 2, size);
            const std::uint32_t from = darkLeft ? 0 : size / 2 + 1;
            const std::uint32_t to = darkLeft ? size / 2 - 1 : size;
            ASSERT_GT(
                litIn(darkLeft ? size / 2 + 1 : 0, darkLeft ? size : size / 2 - 1), std::size_t{ size } * size / 8)
                << "the lamp's side is not lit";

            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = from; x < to; ++x)
                {
                    const std::size_t pixel = std::size_t{ y } * size + x;
                    EXPECT_EQ(plain[pixel], 0.0f) << "the plain bounce lit the dark side at " << x << ", " << y;
                    EXPECT_EQ(reused[pixel], 0.0f) << "light reached the dark side at " << x << ", " << y;
                }
        }

        /// **The reuse keeps its history while the eye moves.** The eye walks along the corner two
        /// units a frame, which carries the wall under it a little under half a pixel at a time and
        /// the floor more: last frame's reservoir is at a point between pixels, and the nearest tap
        /// holds it (`heldBefore` says why not one drawn by its share). Rounding loses a reservoir
        /// wherever the step nears half a pixel, which this pace does, so the temporal half keeps
        /// the error to three fifths of no reuse and not to the quarter a still eye's is: held under
        /// two thirds, which a history lost at every step would not be.
        ///
        /// The truth is 160 plain frames at the walk's end. Each run walks forty frames from a cut,
        /// four times from four draws, and is held at its last frame.
        TEST_F(RtxBounceReuseTest, theReuseKeepsItsHistoryWhileTheEyeMoves)
        {
            constexpr std::uint32_t size = 128;
            constexpr std::uint32_t steps = 40;
            constexpr std::uint32_t walks = 4;
            constexpr float pace = 2.0f;
            constexpr float end = 80.0f;
            const SceneDesc scene = makeCorner();

            const auto standing = [&](float x, std::uint32_t frame) {
                Shaders::VisibilityConstants camera = makeCamera(
                    osg::Vec3f(x, -200.0f, 50.0f), osg::Vec3f(x, 300.0f, -100.0f), 60.0f, size, size, 10000.0f);
                const Shaders::VisibilityConstants lit = cornerCamera(size);
                camera.mSun = lit.mSun;
                camera.mSkyHorizon = lit.mSkyHorizon;
                camera.mSkyZenith = lit.mSkyZenith;
                camera.mAmbientFromSky = lit.mAmbientFromSky;
                camera.mFrame = frame;
                return camera;
            };

            mRenderer.resize(size, size);
            mRenderer.setScene(Rtx::SceneSlot::world(), scene, {});

            std::vector<float> read;
            const auto draw = [&](float x, std::uint32_t frame, BounceReuseRule reuse, bool cut) {
                mRenderer.renderFrame(standing(x, frame),
                    FrameOptions{ .mLoss = cut ? HistoryLoss::Cut : HistoryLoss::None,
                        .mReconstruction = ReconstructionRequest{ .mDenoise = false, .mBounceReuse = reuse },
                        .mExposure = FixedExposure{ 1.0f } });
                ASSERT_TRUE(mRenderer.finishFrame().has_value());
            };
            const auto bounce = [&] {
                mRenderer.readChannel(Channel::Indirect, read);
                return luminanceOf(read);
            };

            std::vector<std::vector<float>> plain;
            for (std::uint32_t frame = 0; frame < 160; ++frame)
            {
                draw(end, 9000 + frame, BounceReuseRule::Off, frame == 0);
                plain.push_back(bounce());
            }
            const std::vector<float> truth = meanOf(plain);

            float errors[3]{};
            const BounceReuseRule reuses[3]{ BounceReuseRule::Own, BounceReuseRule::Temporal,
                BounceReuseRule::Spatiotemporal };
            for (std::size_t at = 0; at < 3; ++at)
            {
                std::vector<std::vector<float>> ends;
                for (std::uint32_t walk = 0; walk < walks; ++walk)
                {
                    for (std::uint32_t step = 0; step <= steps; ++step)
                        draw(end - static_cast<float>(steps - step) * pace, 30000 + walk * 100 + step, reuses[at],
                            step == 0);
                    ends.push_back(bounce());
                }
                EXPECT_NEAR(wholeOf(meanOf(ends)) / wholeOf(truth), 1.0f, 0.02f)
                    << sBounceReuseRuleNames.name(reuses[at]);
                errors[at] = errorOf(ends, truth);
            }

            EXPECT_LT(errors[1], 0.67f * errors[0])
                << "the walk lost the temporal half's history: " << errors[1] << " against " << errors[0];
            EXPECT_LT(errors[2], errors[1]) << "the neighbours took nothing off a walking frame";
        }

        /// **The temporal reuse carries no firefly.** A lamp two units off the wall lights a spot of
        /// it hundreds of times brighter than the rest of the corner, which a dim sky lights, so a
        /// bounce that finds the spot is a rare and very bright candidate. Merged into the next
        /// frame, a candidate like it keeps most of its weight for many frames; the boiling filter
        /// lets a carried sample go past `BOUNCE_BOILING_LIMIT` times its workgroup's mean.
        ///
        /// Counted over a run, the pixels that stand past that multiple of the run's mean: the
        /// plain bounce drew 24, and the temporal reuse showed 37 without the filter and 18 with it.
        /// **Held at no more than the plain bounce draws**, and the run's mean within 2% of its own.
        TEST_F(RtxBounceReuseTest, theTemporalReuseCarriesNoFirefly)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = cornerCamera(size);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f(0.02f, 0.02f, 0.02f);
            camera.mSkyZenith = osg::Vec3f(0.02f, 0.02f, 0.02f);

            struct Run
            {
                double mMean = 0.0;
                std::size_t mBright = 0;
            };
            const auto runOf = [&](BounceReuseRule reuse) {
                SceneDesc scene = makeCorner();
                scene.addLight(Light{
                    .mPosition = osg::Vec3f(0.0f, 298.0f, 0.0f),
                    .mIntensity = osg::Vec3f(400.0f, 400.0f, 400.0f),
                    .mReach = 1000.0f,
                });
                std::vector<float> read;
                std::vector<float> green;
                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = 64,
                        .mAverage = false,
                        .mFirstFrame = 100,
                        .mBounceReuse = reuse,
                        .mLoss = HistoryLoss::Cut,
                        .mEachFrame = [&](const Frame&) {
                            mRenderer.readChannel(Channel::Indirect, read);
                            for (std::size_t pixel = 0; pixel < read.size() / 4; ++pixel)
                                green.push_back(read[pixel * 4 + 1]);
                        } });

                Run run;
                for (const float value : green)
                    run.mMean += double(value) / double(green.size());
                for (const float value : green)
                    run.mBright += double(value) > run.mMean * double(Shaders::BOUNCE_BOILING_LIMIT) ? 1 : 0;
                return run;
            };

            const Run plain = runOf(BounceReuseRule::Off);
            const Run reused = runOf(BounceReuseRule::Temporal);
            ASSERT_GT(plain.mBright, 0u) << "the spot is no firefly";
            EXPECT_LE(reused.mBright, plain.mBright) << "the reuse carried a firefly";
            EXPECT_NEAR(reused.mMean / plain.mMean, 1.0, 0.02);
        }

        /// **A bounce from a lamp that went out is gone in eight frames.** The corner with no sky and
        /// no sun, lit by a lamp before the wall, so the floor's bounce is the wall's lamplight and
        /// nothing else; the lamp goes, and the bounce is nought exactly once every sample a reservoir
        /// kept from it is gone.
        ///
        /// Resampling keeps them: no candidate the dark frames draw outweighs one, and without the
        /// validation 87% of the bounce was left the frame after and the last of it lasted to the age
        /// cap, thirty frames. The validation asks one pixel of each block of
        /// `BOUNCE_VALIDATION_ACROSS` × `BOUNCE_VALIDATION_DOWN` a frame, each in turn, and a sample
        /// whose light is gone takes nought: **the eighth frame after is dark**, and the seventh,
        /// with one pixel in eight not yet asked, is not. The plain bounce is dark on the first.
        TEST_F(RtxBounceReuseTest, aBounceFromALampThatWentOutIsGoneInEightFrames)
        {
            constexpr std::uint32_t size = 64;
            constexpr std::uint32_t asked = Shaders::BOUNCE_VALIDATION_ACROSS * Shaders::BOUNCE_VALIDATION_DOWN;
            Shaders::VisibilityConstants camera = cornerCamera(size);
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mAmbientFromSky = 0.0f;

            for (const BounceReuseRule reuse :
                { BounceReuseRule::Off, BounceReuseRule::Temporal, BounceReuseRule::Spatiotemporal })
            {
                SceneDesc scene = makeCorner();
                scene.addLight(Light{
                    .mPosition = osg::Vec3f(0.0f, 250.0f, 0.0f),
                    .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                    .mReach = 1000.0f,
                });

                std::vector<float> read;
                const auto lit = [&] {
                    mRenderer.readChannel(Channel::Indirect, read);
                    std::size_t pixels = 0;
                    for (std::size_t pixel = 0; pixel < read.size() / 4; ++pixel)
                        pixels
                            += std::max({ read[pixel * 4], read[pixel * 4 + 1], read[pixel * 4 + 2] }) > 0.0f ? 1 : 0;
                    return pixels;
                };

                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = 48,
                        .mAverage = false,
                        .mFirstFrame = 100,
                        .mBounceReuse = reuse,
                        .mLoss = HistoryLoss::Cut });
                ASSERT_GT(lit(), std::size_t{ size } * size / 4) << sBounceReuseRuleNames.name(reuse);

                // The lamp goes: the per-frame lists are emptied and the corner placed again.
                scene.clearPlacement();
                std::vector<std::size_t> after;
                shoot(scene, {}, camera, size,
                    Shot{ .mFrames = asked,
                        .mAverage = false,
                        .mFirstFrame = 148,
                        .mBounceReuse = reuse,
                        .mSetScene = false,
                        .mEachFrame = [&](const Frame&) { after.push_back(lit()); } });

                const std::size_t darkFrom = reuse == BounceReuseRule::Off ? 0 : asked - 1;
                for (std::size_t frame = 0; frame < after.size(); ++frame)
                    if (frame < darkFrom)
                        EXPECT_GT(after[frame], 0u) << sBounceReuseRuleNames.name(reuse) << ", frame " << frame;
                    else
                        EXPECT_EQ(after[frame], 0u) << sBounceReuseRuleNames.name(reuse) << ", frame " << frame;
            }
        }

        /// **A pixel whose surface went keeps only its candidate.** A red pillar stands before the
        /// corner's wall; after sixteen frames of the temporal reuse it is moved behind the eye, and
        /// the wall and the floor it stood in front of are pixels whose last reservoirs were the
        /// pillar's. `heldSurfaceMatches` refuses those, so each shows its own candidate: the frame
        /// the reuse `own` shows, bit for bit, wherever the pillar stood a pixel in from its edge,
        /// where the fetch's nearest tap can be a pixel beside it. Everywhere else the history is
        /// kept, and the frame is not the candidate's.
        TEST_F(RtxBounceReuseTest, aPixelWhoseSurfaceWentKeepsOnlyItsCandidate)
        {
            constexpr std::uint32_t size = 64;
            std::vector<float> albedo;
            const auto after = [&](BounceReuseRule reuse) {
                SceneDesc scene = makeCorner();
                const Index red = scene.addMaterial(Material{ .mDiffuseColour = osg::Vec3f(0.8f, 0.1f, 0.1f) });
                const Index pillar = addQuad(scene, uprightQuadAt(60.0f, 100.0f, osg::Vec2f(0.0f, -40.0f)), red);
                shoot(scene, {}, cornerCamera(size), size,
                    Shot{ .mFrames = 16,
                        .mAverage = false,
                        .mFirstFrame = 300,
                        .mBounceReuse = reuse,
                        .mLoss = HistoryLoss::Cut });
                mRenderer.readChannel(Channel::Albedo, albedo);

                scene.placements().move(pillar, osg::Matrixf::translate(0.0f, -1000.0f, 0.0f));
                Shaders::VisibilityConstants camera = cornerCamera(size);
                camera.mFrame = 316;
                shoot(scene, {}, camera, size, Shot{ .mBounceReuse = reuse, .mSetScene = false });
                std::vector<float> indirect;
                mRenderer.readChannel(Channel::Indirect, indirect);
                return indirect;
            };

            const std::vector<float> own = after(BounceReuseRule::Own);
            const std::vector<float> temporal = after(BounceReuseRule::Temporal);
            ASSERT_EQ(own.size(), temporal.size());

            const auto wasPillar = [&](std::uint32_t x, std::uint32_t y) {
                const float* const at = &albedo[(std::size_t{ y } * size + x) * 4];
                return at[0] > 0.7f && at[1] < 0.2f;
            };
            std::size_t went = 0;
            std::size_t kept = 0;
            for (std::uint32_t y = 1; y + 1 < size; ++y)
                for (std::uint32_t x = 1; x + 1 < size; ++x)
                {
                    const std::size_t pixel = std::size_t{ y } * size + x;
                    const bool inside = wasPillar(x, y) && wasPillar(x - 1, y) && wasPillar(x + 1, y)
                        && wasPillar(x, y - 1) && wasPillar(x, y + 1);
                    const bool same = std::equal(&own[pixel * 4], &own[pixel * 4 + 4], &temporal[pixel * 4]);
                    if (inside)
                    {
                        ++went;
                        EXPECT_TRUE(same) << "the pillar's history reached " << x << ", " << y;
                    }
                    else if (!wasPillar(x, y))
                        kept += same ? 0 : 1;
                }

            ASSERT_GT(went, std::size_t{ size } * size / 16) << "the pillar covered too little";
            EXPECT_GT(kept, std::size_t{ size } * size / 2) << "the history went everywhere";
        }
    }
}
