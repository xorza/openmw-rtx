#include <array>
#include <cstddef>
#include <cstdint>
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
        /// A floor under an overhead sun, seen from above, with the sky black so the sun is the only
        /// light: the bounce escapes to nothing and the frame is the sun's term alone.
        Shaders::VisibilityConstants overheadSun(std::uint32_t size)
        {
            Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, -1.0f, 300.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), 60.0f, size, size, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(2.0f, 2.0f, 2.0f));
            return camera;
        }

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

        /// A roof over the floor, facing up, so a shadow ray from the floor meets its back face,
        /// which the light's rule does not cull: `x` from `left` to `right` at `height`.
        std::array<osg::Vec3f, 4> roofOver(float left, float right, float height)
        {
            return {
                osg::Vec3f(left, -4000.0f, height),
                osg::Vec3f(right, -4000.0f, height),
                osg::Vec3f(right, 4000.0f, height),
                osg::Vec3f(left, 4000.0f, height),
            };
        }

        /// **A floor every ray reaches, and a floor no ray reaches, come back exact.** Every tile of
        /// either is cleared — each receiver and its neighbours alike — so nothing is filtered, and
        /// the composite multiplies the sun by exactly one or exactly nought. The frame the shadow
        /// denoiser composes is then the frame the trace composes with the rays' own bits, value for
        /// value. The bits themselves are read off `CHANNEL_SUNLIT`: all one under the open sky, and
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
                    mRenderer.readChannel(Channel::Sunlit, sunlit);
                    ASSERT_EQ(sunlit.size(), std::size_t{ size } * size * 4);
                    for (std::size_t value = 0; value < sunlit.size(); value += 4)
                    {
                        ASSERT_GT(sunlit[value], 0.0f) << "the sun lights every pixel of the floor, roofed or not";
                        ASSERT_EQ(sunlit[value + 3], roofed ? 0.0f : 1.0f) << "pixel " << value / 4;
                    }

                    const Frame filtered = shoot(scene, {}, camera, size,
                        { .mSea = SeaState{ .mSignificantHeight = 0.0f }, .mFilter = true, .mResetHistory = true });
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
                        .mResetHistory = true });

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

        /// **A pixel of water keeps the bit of what it reflects or of what it shows, drawn by the light
        /// each adds.** Water under `grazingTheWater`'s eye, so every pixel's two rays are parallel
        /// to every other's. The reflection meets `sGrazedWall`, under an overhang 150 deep that
        /// shades all of it from a sun 45 degrees up behind the eye. The refraction meets a bed fifty
        /// units down, where the sun reaches: at most `-100 + 1.09 * 50` = -45 along, where the sun's
        /// path back rises 50 units in the water at 32 degrees off the vertical and 100 more in air,
        /// 131 units toward the sun, to -176 — short of the overhang, which starts at -150.
        ///
        /// The wall alone and the bed alone say what each adds, and together the sum of the two
        /// stands in the channel. The bit is then one — the bed's — in the share the bed's luminance
        /// is of the two, over 64 frames of every pixel; a pixel that kept the brighter source's bit
        /// would hold the same one every frame.
        TEST_F(RtxVisibilityTest, aPixelOfWaterKeepsTheShadowOfEitherRayByTheLightItAdds)
        {
            constexpr std::uint32_t size = 32;
            constexpr std::uint32_t frames = 64;
            constexpr std::size_t pixels = std::size_t{ size } * size;

            Shaders::VisibilityConstants camera = grazingTheWater(size);
            litThroughWater(camera, osg::DegreesToRadians(45.0f));
            camera.mAmbient = osg::Vec3f();

            const auto sceneOf = [](bool wall, bool bed) {
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
                return scene;
            };

            const auto sunOf = [&](bool wall, bool bed) {
                shoot(sceneOf(wall, bed), {}, camera, size, { .mSea = SeaState{ .mSignificantHeight = 0.0f } });
                std::vector<float> read;
                mRenderer.readChannel(Channel::Sunlit, read);
                return read;
            };

            const std::vector<float> reflected = sunOf(true, false);
            const std::vector<float> refracted = sunOf(false, true);
            for (std::size_t pixel = 0; pixel < pixels; ++pixel)
            {
                ASSERT_EQ(reflected[pixel * 4 + 3], 0.0f) << "the overhang shades the wall at pixel " << pixel;
                ASSERT_EQ(refracted[pixel * 4 + 3], 1.0f) << "the sun reaches the bed at pixel " << pixel;
            }

            std::vector<float> sunlit;
            std::vector<double> kept(pixels, 0.0);
            const auto keep = [&](const Frame&) {
                mRenderer.readChannel(Channel::Sunlit, sunlit);
                for (std::size_t pixel = 0; pixel < pixels; ++pixel)
                    kept[pixel] += static_cast<double>(sunlit[pixel * 4 + 3]);
            };
            shoot(sceneOf(true, true), {}, camera, size,
                { .mSea = SeaState{ .mSignificantHeight = 0.0f },
                    .mFrames = frames,
                    .mAverage = false,
                    .mEachFrame = keep });

            double share = 0.0;
            double drawn = 0.0;
            for (std::size_t pixel = 0; pixel < pixels; ++pixel)
            {
                const osg::Vec3f wall(reflected[pixel * 4], reflected[pixel * 4 + 1], reflected[pixel * 4 + 2]);
                const osg::Vec3f bed(refracted[pixel * 4], refracted[pixel * 4 + 1], refracted[pixel * 4 + 2]);
                const osg::Vec3f both(sunlit[pixel * 4], sunlit[pixel * 4 + 1], sunlit[pixel * 4 + 2]);
                for (int channel = 0; channel < 3; ++channel)
                    ASSERT_NEAR(both[channel], wall[channel] + bed[channel], 1e-5f * both[channel])
                        << "the channel holds both sources' light at pixel " << pixel;

                const float fromBed = bed * Shaders::LUMINANCE_WEIGHTS;
                share += static_cast<double>(fromBed / (fromBed + wall * Shaders::LUMINANCE_WEIGHTS));
                drawn += kept[pixel] / frames;
            }
            share /= pixels;
            drawn /= pixels;

            ASSERT_GT(share, 0.2) << "the wall outshines the bed, or this proves nothing";
            ASSERT_LT(share, 0.8) << "the bed outshines the wall, or this proves nothing";
            // A bit drawn independently at every pixel of every frame: a deviation of
            // `sqrt(p (1 - p) / 65536)`, 0.002 at most. Measured: a share of 0.559, and 0.557 drawn.
            EXPECT_NEAR(drawn, share, 0.01) << "the bit kept is not the bed's in the bed's share";
        }
    }
}
