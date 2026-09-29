#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/scene/scenedesc.hpp>
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
        /// all nought under the roof, with the sun's light standing behind both.
        TEST_F(RtxVisibilityTest, theShadowDenoiserLeavesAFloorWhollyLitOrWhollyShadowedExact)
        {
            constexpr std::uint32_t size = 64;
            const Shaders::VisibilityConstants camera = overheadSun(size);

            for (const bool roofed : { false, true })
            {
                SceneDesc scene;
                addQuad(scene, sheetAt(4000.0f, 0.0f));
                if (roofed)
                    addQuad(scene, roofOver(-4000.0f, 4000.0f, 2000.0f));

                const Frame raw = shoot(scene, {}, camera, size);

                std::vector<float> sunlit;
                mRenderer.readChannel(Channel::Sunlit, sunlit);
                ASSERT_EQ(sunlit.size(), std::size_t{ size } * size * 4);
                for (std::size_t value = 0; value < sunlit.size(); value += 4)
                {
                    ASSERT_GT(sunlit[value], 0.0f) << "the sun lights every pixel of the floor, roofed or not";
                    ASSERT_EQ(sunlit[value + 3], roofed ? 0.0f : 1.0f) << "pixel " << value / 4;
                }

                const Frame filtered = shoot(scene, {}, camera, size, { .mFilter = true, .mResetHistory = true });
                EXPECT_EQ(filtered.mRadiance, raw.mRadiance) << (roofed ? "roofed" : "open");
            }
        }

        /// **The noise of a penumbra comes off and its light stays where it was.** A roof over half
        /// the floor, two thousand units up, under a sun whose shadow cone is `SUN_SHADOW_RADIUS`
        /// either way: the penumbra is `2 * 2000 * 0.0349` = 140 units across, a quarter of the
        /// 346 the frame spans, and one ray a pixel speckles it. Measured against a reference of 256
        /// unfiltered frames, one frame's error and the denoised frame's after sixteen frames of
        /// history.
        TEST_F(RtxVisibilityTest, theShadowDenoiserTakesTheNoiseOffAPenumbraAndLeavesItsLightWhereItWas)
        {
            constexpr std::uint32_t size = 64;
            Shaders::VisibilityConstants camera = overheadSun(size);

            SceneDesc scene;
            addQuad(scene, sheetAt(4000.0f, 0.0f));
            addQuad(scene, roofOver(-4000.0f, 0.0f, 2000.0f));

            const Frame reference = shoot(scene, {}, camera, size, { .mFrames = 256 });

            camera.mFrame = 1000;
            const Frame raw = shoot(scene, {}, camera, size);

            const Frame denoised = shoot(scene, {}, camera, size,
                { .mFrames = 16, .mAverage = false, .mFirstFrame = 2000, .mFilter = true, .mResetHistory = true });

            const float rawError = raw.errorFrom(reference);
            const float denoisedError = denoised.errorFrom(reference);
            ASSERT_GT(rawError, 0.01f) << "a penumbra one ray a pixel draws is noisy, or this proves nothing";
            // Measured at a thirteenth: 0.0762 raw against 0.0058 denoised.
            EXPECT_LT(denoisedError, rawError * 0.1f) << "raw " << rawError << ", denoised " << denoisedError;

            // **The one bias the port keeps is the SDK's clamp**: the history is held to half a
            // deviation of the local mean, which on a penumbra's gradient holds it a little under
            // its own mean — measured at 0.64% here. The SDK's contrast step, which darkened the
            // penumbra by a further 3.4% on purpose, is not ported, and the frame's edge no longer
            // counts as shadow; either would fail this.
            EXPECT_NEAR(denoised.mean(), reference.mean(), reference.mean() * 0.01f)
                << "the denoiser moved the light it was smoothing";
        }
    }
}
