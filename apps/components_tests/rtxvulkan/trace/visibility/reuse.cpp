#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
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

        class RtxBounceReuseTest : public RtxVisibilityTest
        {
        protected:
            /// The bounce's luminance at every pixel of the corner, one vector a frame, over a run of
            /// `frames` from the sampler's frame `first` on with the history let build from nothing,
            /// past the first `skipped`.
            std::vector<std::vector<float>> bounceRun(BounceReuse reuse, std::uint32_t size, std::uint32_t frames,
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
                            std::vector<float>& luminance = kept.emplace_back(read.size() / 4);
                            for (std::size_t pixel = 0; pixel < luminance.size(); ++pixel)
                                luminance[pixel] = 0.2126f * read[pixel * 4] + 0.7152f * read[pixel * 4 + 1]
                                    + 0.0722f * read[pixel * 4 + 2];
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
            ASSERT_EQ(shoot(scene, {}, camera, sSize, Shot{ .mBounceReuse = BounceReuse::Off }).mHits, sSize * sSize);
            mRenderer.readChannel(Channel::Indirect, plain);
            mRenderer.readChannel(Channel::Fill, plainFill);

            std::vector<float> own;
            std::vector<float> ownFill;
            ASSERT_EQ(shoot(scene, {}, camera, sSize, Shot{ .mBounceReuse = BounceReuse::Own }).mHits, sSize * sSize);
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
        /// The truth is 512 plain frames averaged. Each run is 96 frames from a cut, of which the
        /// last 64 are read, so every reservoir has filled to its cap. **The mean is held to two per
        /// cent**: the plain bounce's own run of 64 stands within a few parts in a thousand of the
        /// truth over these thousand pixels, and the reuse's frames are correlated, which widens what
        /// their mean wanders by. **The error of one frame falls by a fifth at least** with the
        /// temporal half and further with the spatial one, which is what the reuse is for.
        TEST_F(RtxBounceReuseTest, theReuseKeepsTheMeanAndTakesNoiseOffEachFrame)
        {
            const std::vector<float> truth = meanOf(bounceRun(BounceReuse::Off, 128, 256, 0, 10000));
            const float whole = wholeOf(truth);
            ASSERT_GT(whole, 0.0f);

            float errors[3]{};
            const BounceReuse reuses[3]{ BounceReuse::Own, BounceReuse::Temporal, BounceReuse::Spatiotemporal };
            for (std::size_t at = 0; at < 3; ++at)
            {
                const std::vector<std::vector<float>> run = bounceRun(reuses[at], 128, 64, 24, 20000);
                EXPECT_NEAR(wholeOf(meanOf(run)) / whole, 1.0f, 0.02f) << sBounceReuseNames.name(reuses[at]);
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
            const Index twoSided = scene.addMaterial(
                Material{ .mDiffuseColour = osg::Vec3f(0.5f, 0.5f, 0.5f), .mTwoSided = true });
            const std::array<osg::Vec3f, 4> wall{ osg::Vec3f(0.0f, -400.0f, 0.0f), osg::Vec3f(0.0f, 400.0f, 0.0f),
                osg::Vec3f(0.0f, 400.0f, 300.0f), osg::Vec3f(0.0f, -400.0f, 300.0f) };
            addQuad(scene, wall, twoSided);
            scene.addLight(Light{
                .mPosition = osg::Vec3f(150.0f, 0.0f, 50.0f),
                .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                .mReach = 1000.0f,
            });

            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, 0.0f, 900.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 60.0f, size, size, 10000.0f);
            camera.mAmbientFromSky = 0.0f;

            const auto lightest = [&](BounceReuse reuse) {
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

            const std::vector<float> plain = lightest(BounceReuse::Off);
            const std::vector<float> reused = lightest(BounceReuse::Spatiotemporal);

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
            ASSERT_GT(litIn(darkLeft ? size / 2 + 1 : 0, darkLeft ? size : size / 2 - 1), std::size_t{ size } * size / 8)
                << "the lamp's side is not lit";

            for (std::uint32_t y = 0; y < size; ++y)
                for (std::uint32_t x = from; x < to; ++x)
                {
                    const std::size_t pixel = std::size_t{ y } * size + x;
                    EXPECT_EQ(plain[pixel], 0.0f) << "the plain bounce lit the dark side at " << x << ", " << y;
                    EXPECT_EQ(reused[pixel], 0.0f) << "light reached the dark side at " << x << ", " << y;
                }
        }
    }
}
