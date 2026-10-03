#include <algorithm>
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
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

namespace Rtx::Testing
{
    namespace
    {
        using RtxBounceReuseTest = RtxVisibilityTest;

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

        Shaders::VisibilityConstants cornerCamera()
        {
            Shaders::VisibilityConstants camera = makeCamera(
                osg::Vec3f(0.0f, -200.0f, 50.0f), osg::Vec3f(0.0f, 300.0f, -100.0f), 60.0f, sSize, sSize, 10000.0f);
            camera.mSun = Shaders::sunSource(osg::Vec3f(0.3f, -0.5f, 0.8f), osg::Vec3f(3.0f, 3.0f, 3.0f));
            camera.mSkyHorizon = osg::Vec3f(0.4f, 0.5f, 0.6f);
            camera.mSkyZenith = osg::Vec3f(0.2f, 0.3f, 0.6f);
            camera.mAmbientFromSky = 1.0f;
            camera.mFrame = 7;
            return camera;
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
    }
}
