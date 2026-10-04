#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/colour.h>
#include <components/rtx/shaders/visibility.h>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        /// The floor the sun lights: level at nought, or a hundred units under water whose surface is
        /// at nought, which `camera` is told — so the sun's bit is what the water's refraction found.
        SceneDesc floorOf(bool flooded, Shaders::VisibilityConstants& camera)
        {
            if (flooded)
            {
                camera.mWaterLevel = 0.0f;
                return makeFlooded(4000.0f, 100.0f);
            }

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            return scene;
        }

        /// **A floor every ray reaches, and a floor no ray reaches, come back exact.** Every tile of
        /// either is cleared — each receiver and its neighbours alike — so nothing is filtered, and
        /// the composite multiplies the sun by exactly one or exactly nought. The frame the shadow
        /// denoiser composes is then the frame the trace composes with the rays' own bits, value for
        /// value. The bits themselves are read off `CHANNEL_SHADOWED`: all one under the open sky, and
        /// all nought under the roof, with the sun's light standing behind both. **And so for a
        /// flooded floor**, whose bit is what the water's refraction found: the reflection finds
        /// the black sky and adds no sun, so the bed's bit is the only one to keep.
        TEST_F(RtxVisibilityTest, theShadowDenoiserLeavesAFloorWhollyLitOrWhollyShadowedExact)
        {
            constexpr std::uint32_t size = 64;

            for (const bool flooded : { false, true })
                for (const bool roofed : { false, true })
                {
                    SCOPED_TRACE(flooded ? "flooded" : "dry");
                    SCOPED_TRACE(roofed ? "roofed" : "open");
                    Shaders::VisibilityConstants camera = overheadSun(size);
                    SceneDesc scene = floorOf(flooded, camera);
                    if (roofed)
                        addQuad(scene, roofOver(-4000.0f, 4000.0f, 2000.0f));

                    const Frame raw
                        = shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });

                    std::vector<float> sunlit;
                    mRenderer.readChannel(Channel::Shadowed, sunlit);
                    ASSERT_EQ(sunlit.size(), std::size_t{ size } * size * 4);
                    for (std::size_t value = 0; value < sunlit.size(); value += 4)
                    {
                        ASSERT_GT(sunlit[value], 0.0f) << "the sun lights every pixel of the floor, roofed or not";
                        ASSERT_EQ(sunlit[value + 3], roofed ? 0.0f : 1.0f) << "pixel " << value / 4;
                    }

                    const Frame filtered = shoot(scene, {}, camera, size,
                        { .mSea = SeaState{ .mSignificantHeight = 0.0f }, .mFilter = true, .mLoss = HistoryLoss::Cut });
                    EXPECT_EQ(filtered.mRadiance, raw.mRadiance);
                }
        }

        /// **The noise of a penumbra comes off and its light stays where it was.** A roof over half
        /// the floor, two thousand units up, under a sun whose shadow cone is `SUN_SHADOW_RADIUS`
        /// either way: the penumbra is `2 * 2000 * 0.0349` = 140 units across, a quarter of the
        /// 346 the frame spans, and one ray a pixel speckles it. Measured against a reference of 256
        /// unfiltered frames, one frame's error and the denoised frame's after sixteen frames of
        /// history — on a dry floor, and on a flooded one seen through the water, whose sun is what
        /// the refraction found. Two tests on one body, since each is 273 frames.
        class RtxPenumbraDenoiseTest : public RtxVisibilityTest
        {
        protected:
            void penumbraOn(bool flooded)
            {
                constexpr std::uint32_t size = 64;
                Shaders::VisibilityConstants camera = overheadSun(size);
                SceneDesc scene = floorOf(flooded, camera);
                addQuad(scene, roofOver(-4000.0f, 0.0f, 2000.0f));

                const Frame reference = shoot(
                    scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f }, .mFrames = 256 });

                camera.mFrame = 1000;
                const Frame raw = shoot(scene, {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });

                const Frame denoised = shoot(scene, {}, camera, size,
                    { .mSea = SeaState{ .mSignificantHeight = 0.0f },
                        .mFrames = 16,
                        .mAverage = false,
                        .mFirstFrame = 2000,
                        .mFilter = true,
                        .mLoss = HistoryLoss::Cut });

                const float rawError = raw.errorFrom(reference);
                const float denoisedError = denoised.errorFrom(reference);
                ASSERT_GT(rawError, 0.01f) << "a penumbra one ray a pixel draws is noisy, or this proves nothing";
                EXPECT_LT(denoisedError, rawError * 0.1f) << "raw " << rawError << ", denoised " << denoisedError;

                // **The one bias the port keeps is the SDK's clamp**: the history is held to half a
                // deviation of the local mean, which on a penumbra's gradient holds it a little under
                // its own mean — measured at 0.64% dry and 0.07% flooded. The SDK's contrast step,
                // which darkened the penumbra by a further 3.4% on purpose, is not ported, and the
                // frame's edge no longer counts as shadow; either would fail this.
                EXPECT_NEAR(denoised.mean(), reference.mean(), reference.mean() * 0.01f)
                    << "the denoiser moved the light it was smoothing";
            }
        };

        /// Measured at a thirteenth: 0.0762 raw against 0.0058 denoised.
        TEST_F(RtxPenumbraDenoiseTest, theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas)
        {
            penumbraOn(false);
        }

        /// Measured at a twelfth: 0.0318 raw against 0.0026 denoised. Before the water split the sun
        /// off what its rays found, nothing filtered it, and the denoised frame stood at 0.0322.
        TEST_F(RtxPenumbraDenoiseTest, theShadowDenoiserTakesTheNoiseOffAPenumbraSeenThroughTheWater)
        {
            penumbraOn(true);
        }

        /// **The lamps' light is the shadow denoiser's, exact where nothing stands in its way and quieter
        /// where something does.** Four lamps of four colours over a floor under a black sky, so the
        /// lamps are the frame's whole light: each pixel's reservoir holds one, weighed against the
        /// other three, and its ray decides the pixel's bit. The light the bit multiplies is every
        /// lamp's summed, so on the open floor, where every bit is one, one frame is the reference.
        /// A square sixty units across, thirty over the floor, casts four overlapping shadows of four
        /// colours, and there the bits are noisy. Measured against 256 unfiltered frames, a raw frame
        /// and a denoised one after sixteen of history.
        TEST_F(RtxVisibilityTest, theLampsLightIsExactOnAnOpenFloorAndTheirShadowsComeOutQuieter)
        {
            constexpr std::uint32_t size = 64;

            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();

            for (const bool blocked : { false, true })
            {
                SCOPED_TRACE(blocked ? "blocked" : "open");
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                if (blocked)
                    addQuad(scene,
                        std::array<osg::Vec3f, 4>{ osg::Vec3f(-30.0f, -30.0f, 30.0f), osg::Vec3f(30.0f, -30.0f, 30.0f),
                            osg::Vec3f(30.0f, 30.0f, 30.0f), osg::Vec3f(-30.0f, 30.0f, 30.0f) });

                const std::array<std::pair<osg::Vec2f, osg::Vec3f>, 4> lamps{ {
                    { osg::Vec2f(-100.0f, -100.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f) },
                    { osg::Vec2f(100.0f, -100.0f), osg::Vec3f(0.0f, 4000.0f, 0.0f) },
                    { osg::Vec2f(-100.0f, 100.0f), osg::Vec3f(0.0f, 0.0f, 4000.0f) },
                    { osg::Vec2f(100.0f, 100.0f), osg::Vec3f(4000.0f, 4000.0f, 0.0f) },
                } };
                for (const auto& [place, intensity] : lamps)
                    scene.addLight(Light{
                        .mPosition = osg::Vec3f(place.x(), place.y(), 60.0f),
                        .mIntensity = intensity,
                        .mReach = 500.0f,
                    });

                // The open floor's one frame is the reference, so a reference of four is as good as
                // one of 256 and a quarter of a second shorter.
                camera.mFrame = 0;
                const Frame reference = shoot(scene, {}, camera, size, { .mFrames = blocked ? 256u : 4u });

                camera.mFrame = 1000;
                const Frame raw = shoot(scene, {}, camera, size);
                const Frame filtered = shoot(scene, {}, camera, size,
                    { .mFrames = 16,
                        .mAverage = false,
                        .mFirstFrame = 2000,
                        .mFilter = true,
                        .mLoss = HistoryLoss::Cut });

                for (std::size_t channel = 0; channel < 3; ++channel)
                {
                    SCOPED_TRACE(channel);
                    const float rawError = raw.errorFrom(reference, channel);
                    const float filteredError = filtered.errorFrom(reference, channel);
                    if (!blocked)
                    {
                        // Measured at 1.0e-7, the rounding of a sum of 256 frames.
                        EXPECT_LT(rawError, 1e-6f) << "one frame of the open floor is not the reference";
                        EXPECT_LT(filteredError, 1e-6f) << "the denoised open floor is not the reference";
                        continue;
                    }

                    ASSERT_GT(rawError, 0.001f)
                        << "four shadows drawn one ray a pixel are noisy, or this proves nothing";
                    // Measured at 0.125 to 0.142 of the raw error, channel by channel.
                    EXPECT_LT(filteredError, rawError * 0.2f) << "raw " << rawError << ", filtered " << filteredError;

                    // Measured within 0.13 to 1.0 per cent. **The ratio estimator's cost**: the bit a
                    // neighbour averages in is another lamp's, so a filtered pixel's visibility is
                    // the lamps' in the shares its neighbours drew, under its own sum's colour.
                    EXPECT_NEAR(filtered.mean(channel), reference.mean(channel), reference.mean(channel) * 0.015f)
                        << "the denoiser moved the light";
                }

                // A floor with no specular map has no lobe, and the lobe's channel holds nought light.
                std::vector<float> lobe;
                mRenderer.readChannel(Channel::Specular, lobe);
                ASSERT_EQ(lobe.size(), std::size_t{ size } * size * 4);
                for (std::size_t value = 0; value < lobe.size(); value += 4)
                    ASSERT_EQ(osg::Vec3f(lobe[value], lobe[value + 1], lobe[value + 2]), osg::Vec3f())
                        << "pixel " << value / 4 << " of a floor with no lobe";
            }
        }

        /// What two shadowed sources one pixel shows come to: the share of the two lights the second
        /// adds, in luminance and over every pixel, and the share of frames the pixel kept its bit.
        struct BitShares
        {
            double mShare;
            double mDrawn;
        };

        /// **Two shadowed sources one pixel shows, and one bit between them.** Each alone first,
        /// read off `CHANNEL_SHADOWED`: the first's ray stopped at every pixel, and the second's
        /// through at every one. Then both, where the channel holds the sum of the two lights, over
        /// `sFrames` frames of every pixel: the bit is one — the second's — in the share the
        /// second's luminance is of the two, and a pixel that kept the brighter source's bit would
        /// hold the same one every frame. Two tests on one body.
        class RtxBitShareTest : public RtxVisibilityTest
        {
        protected:
            static constexpr std::uint32_t sSize = 32;
            static constexpr std::uint32_t sFrames = 64;

            /// @param shootWith shoots the scene with the first source, the second, or both, as its
            ///        two flags say, under the shot it is handed.
            BitShares sharesOf(const std::function<void(bool first, bool second, const Shot& shot)>& shootWith)
            {
                constexpr std::size_t pixels = std::size_t{ sSize } * sSize;

                const auto alone = [&](bool first, bool second) {
                    shootWith(first, second, {});
                    std::vector<float> read;
                    mRenderer.readChannel(Channel::Shadowed, read);
                    return read;
                };

                const std::vector<float> stopped = alone(true, false);
                const std::vector<float> open = alone(false, true);
                for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                {
                    EXPECT_EQ(stopped[pixel * 4 + 3], 0.0f) << "the first source's ray got through at " << pixel;
                    EXPECT_EQ(open[pixel * 4 + 3], 1.0f) << "the second source's ray was stopped at " << pixel;
                }

                std::vector<float> both;
                std::vector<double> kept(pixels, 0.0);
                shootWith(true, true, { .mFrames = sFrames, .mAverage = false, .mEachFrame = [&](const Frame&) {
                                           mRenderer.readChannel(Channel::Shadowed, both);
                                           for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                                               kept[pixel] += static_cast<double>(both[pixel * 4 + 3]);
                                       } });

                BitShares shares{ .mShare = 0.0, .mDrawn = 0.0 };
                for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                {
                    const osg::Vec3f first(stopped[pixel * 4], stopped[pixel * 4 + 1], stopped[pixel * 4 + 2]);
                    const osg::Vec3f second(open[pixel * 4], open[pixel * 4 + 1], open[pixel * 4 + 2]);
                    const osg::Vec3f sum(both[pixel * 4], both[pixel * 4 + 1], both[pixel * 4 + 2]);
                    for (int channel = 0; channel < 3; ++channel)
                        EXPECT_NEAR(sum[channel], first[channel] + second[channel], 1e-5f * sum[channel])
                            << "the channel holds both sources' light at pixel " << pixel;

                    const float fromSecond = second * Shaders::LUMINANCE_WEIGHTS;
                    shares.mShare
                        += static_cast<double>(fromSecond / (fromSecond + first * Shaders::LUMINANCE_WEIGHTS));
                    shares.mDrawn += kept[pixel] / sFrames;
                }
                shares.mShare /= pixels;
                shares.mDrawn /= pixels;

                EXPECT_GT(shares.mShare, 0.2) << "the first source outshines the second, or this proves nothing";
                EXPECT_LT(shares.mShare, 0.8) << "the second source outshines the first, or this proves nothing";
                return shares;
            }
        };

        /// **A surface lit by the sun and by a lamp keeps the bit of either by the light it adds.** A
        /// floor under `overheadSun`, roofed two thousand units up, so the sun's ray is stopped at
        /// every pixel, and a lamp sixty units over the floor under the roof, which every pixel sees.
        ///
        /// A bit drawn independently at every pixel of every frame: a deviation of
        /// `sqrt(p (1 - p) / 65536)`, 0.002 at most. Measured: a share of 0.323, and 0.321 drawn.
        TEST_F(RtxBitShareTest, aSurfaceLitByTheSunAndALampKeepsTheBitOfEitherByTheLightItAdds)
        {
            const BitShares shares = sharesOf([&](bool sun, bool lamp, const Shot& shot) {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                addQuad(scene, roofOver(-4000.0f, 4000.0f, 2000.0f));
                if (lamp)
                    scene.addLight(Light{
                        .mPosition = osg::Vec3f(0.0f, 0.0f, 60.0f),
                        .mIntensity = osg::Vec3f(40000.0f, 40000.0f, 40000.0f),
                        .mReach = 500.0f,
                    });

                Shaders::VisibilityConstants camera = overheadSun(sSize);
                if (!sun)
                    camera.mSun.mIrradiance = osg::Vec3f();
                shoot(scene, {}, camera, sSize, shot);
            });

            EXPECT_NEAR(shares.mDrawn, shares.mShare, 0.01) << "the bit kept is not the lamp's in the lamp's share";
        }

        /// **A pixel of water keeps the bit of what it reflects or of what it shows, drawn by the light
        /// each adds.** Water under `grazingTheWater`'s eye, so every pixel's two rays are parallel
        /// to every other's. The reflection meets `sGrazedWall`, under an overhang 150 deep that
        /// shades all of it from a sun 45 degrees up behind the eye. The refraction meets a bed fifty
        /// units down, where the sun reaches: at most `-100 + 1.09 * 50` = -45 along, where the sun's
        /// path back rises 50 units in the water at 32 degrees off the vertical and 100 more in air,
        /// 131 units toward the sun, to -176 — short of the overhang, which starts at -150.
        ///
        /// Measured: a share of 0.559, and 0.557 drawn.
        TEST_F(RtxBitShareTest, aPixelOfWaterKeepsTheShadowOfEitherRayByTheLightItAdds)
        {
            Shaders::VisibilityConstants camera = grazingTheWater(sSize);
            litThroughWater(camera, osg::DegreesToRadians(45.0f));
            camera.mAmbient = osg::Vec3f();

            const BitShares shares = sharesOf([&](bool wall, bool bed, Shot shot) {
                SceneDesc scene = makeOpenWater(4000.0f);
                if (wall)
                {
                    addQuad(scene, sGrazedWall);
                    addQuad(scene,
                        std::array<osg::Vec3f, 4>{ osg::Vec3f(-50.0f, -150.0f, 100.0f),
                            osg::Vec3f(50.0f, -150.0f, 100.0f), osg::Vec3f(50.0f, 0.0f, 100.0f),
                            osg::Vec3f(-50.0f, 0.0f, 100.0f) });
                }
                if (bed)
                    addQuad(scene, sheetAt(4000.0f, -50.0f));

                shot.mSea = SeaState{ .mSignificantHeight = 0.0f };
                shoot(scene, {}, camera, sSize, shot);
            });

            EXPECT_NEAR(shares.mDrawn, shares.mShare, 0.01) << "the bit kept is not the bed's in the bed's share";
        }
    }
}
