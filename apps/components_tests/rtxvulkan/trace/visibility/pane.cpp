#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <apps/components_tests/rtx/support/geometry.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/scene/light.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/visibility.h>

#include "fixture.hpp"

namespace Rtx::Testing
{
    namespace
    {
        constexpr std::uint32_t sSize = 64;

        /// A grey pane at half its opacity, the whole frame wide, 200 units in front of an eye at the
        /// origin, with nothing behind it but a black sky — and four lamps of four colours a hundred
        /// units in front of it. The lamps' one draw a pixel, weighed by its luminance, is the whole
        /// of the pane's noise, and the pane is the whole of the frame's light.
        struct PaneUnderLamps
        {
            SceneDesc mScene;

            /// The pane's slot, which a test moves it by.
            Index mPane;
        };

        PaneUnderLamps paneUnderLamps()
        {
            constexpr float away = 200.0f;

            SceneDesc scene;
            const Index pane = addPane(scene, uprightQuadAt(4000.0f, away), osg::Vec4f(0.5f, 0.5f, 0.5f, 0.5f));

            const std::array<std::pair<osg::Vec2f, osg::Vec3f>, 4> lamps{ {
                { osg::Vec2f(-100.0f, -100.0f), osg::Vec3f(4000.0f, 0.0f, 0.0f) },
                { osg::Vec2f(100.0f, -100.0f), osg::Vec3f(0.0f, 4000.0f, 0.0f) },
                { osg::Vec2f(-100.0f, 100.0f), osg::Vec3f(0.0f, 0.0f, 4000.0f) },
                { osg::Vec2f(100.0f, 100.0f), osg::Vec3f(4000.0f, 4000.0f, 0.0f) },
            } };
            for (const auto& [place, intensity] : lamps)
                scene.addLight(Light{
                    .mPosition = osg::Vec3f(place.x(), away - 100.0f, place.y()),
                    .mIntensity = intensity,
                    .mReach = 500.0f,
                });

            return PaneUnderLamps{ .mScene = std::move(scene), .mPane = pane };
        }

        /// An eye at `eye` looking along +Y, under a black sky with no sun and no ambient.
        Shaders::VisibilityConstants darkEyeAt(const osg::Vec3f& eye)
        {
            Shaders::VisibilityConstants camera
                = Testing::makeCamera(eye, eye + osg::Vec3f(0.0f, 100.0f, 0.0f), 60.0f, sSize, sSize, 100000.0f);
            camera.mSkyHorizon = osg::Vec3f();
            camera.mSkyZenith = osg::Vec3f();
            camera.mSun.mIrradiance = osg::Vec3f();
            camera.mAmbient = osg::Vec3f();
            return camera;
        }

        /// **Over a still eye the pane filter is the mean of its frames.** Sixteen frames filtered from
        /// an empty history, against the same sixteen averaged unfiltered. Under sixteen frames the
        /// blend's weight is one over the count, so the history is the running mean exactly, to the
        /// rounding of its floats: measured at 8e-6 of the raw frame's error, and 2e-6 of the light.
        /// Nothing behind the pane has any light, so the whole of the difference from the last raw
        /// frame is the pane filter's.
        TEST_F(RtxVisibilityTest, overAStillEyeThePaneFilterIsTheMeanOfItsFrames)
        {
            const SceneDesc scene = paneUnderLamps().mScene;
            Shaders::VisibilityConstants camera = darkEyeAt(osg::Vec3f());

            const Frame averaged = shoot(scene, {}, camera, sSize, { .mFrames = 16, .mFirstFrame = 2000 });
            const Frame filtered = shoot(scene, {}, camera, sSize, filteredRun(16, 2000));

            camera.mFrame = 2015;
            const Frame raw = shoot(scene, {}, camera, sSize);

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float rawError = raw.errorFrom(averaged, channel);
                const float filteredError = filtered.errorFrom(averaged, channel);
                ASSERT_GT(rawError, averaged.mean(channel) * 0.05f)
                    << "channel " << channel << ": four lamps drawn one a pixel are noisy";
                EXPECT_LT(filteredError, rawError * 1e-4f)
                    << "channel " << channel << ": raw " << rawError << ", filtered " << filteredError;
                EXPECT_NEAR(filtered.mean(channel), averaged.mean(channel), averaged.mean(channel) * 1e-5f)
                    << "channel " << channel << " keeps its light";
            }
        }

        /// **A pane's history is its own, and goes where the pane goes.** Sixteen filtered frames of
        /// the pane, and then the next frame with the pane moved behind the eye and the history kept:
        ///
        /// - with a second pane a hundred units behind it, which stood still, so `heldSurfaceMatches`
        ///   says the history is another surface's, and the frame is that frame's raw one: none of
        ///   the first pane's light carries over, and none is lost;
        /// - alone, and the frame is black: nothing of the pane is left standing.
        TEST_F(RtxVisibilityTest, aPanesHistoryIsItsOwnAndGoesWhereThePaneGoes)
        {
            Shaders::VisibilityConstants camera = darkEyeAt(osg::Vec3f());
            camera.mFrame = 2016;

            const auto goneAfterSixteen = [&](bool another, bool filtered) {
                PaneUnderLamps lit = paneUnderLamps();
                if (another)
                    addPane(lit.mScene, uprightQuadAt(4000.0f, 300.0f), osg::Vec4f(0.5f, 0.5f, 0.5f, 0.5f));
                shoot(lit.mScene, {}, camera, sSize, filteredRun(16, 2000));
                lit.mScene.placements().move(lit.mPane, osg::Matrixf::translate(0.0f, -1000.0f, 0.0f));
                return shoot(lit.mScene, {}, camera, sSize, { .mFilter = filtered, .mSetScene = false });
            };

            const Frame raw = goneAfterSixteen(true, false);
            const Frame filtered = goneAfterSixteen(true, true);
            ASSERT_GT(raw.mean(0), 0.0f) << "a pane that shows nothing proves nothing";
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_LT(filtered.errorFrom(raw, channel), raw.mean(channel) * 1e-3f)
                    << "channel " << channel << " carried the old pane's light";

            const Frame gone = goneAfterSixteen(false, true);
            for (std::size_t channel = 0; channel < 3; ++channel)
                EXPECT_EQ(gone.mean(channel), 0.0f) << "channel " << channel << " kept the pane's light";
        }

        /// **A pane the eye walks toward keeps its history.** Ten units nearer the pane 200 units
        /// ahead: the pane's own channel carries the step in distance, `frame.cpp`'s arithmetic at
        /// 190 units — `sqrt(200² + 2 (190a)²) - 190 sqrt(1 + 2a²)` = 9.9992 for `a = tan 30° / 64`
        /// — and the history, matched at the distance it was measured from, carries sixteen frames
        /// across the step: the filtered frame stands under half the raw frame's error from the
        /// average at the new eye. Matched at this frame's distance, the step is past
        /// `ACCUMULATE_DEPTH` of 190 units and the history is another surface's.
        TEST_F(RtxVisibilityTest, aPaneTheEyeWalksTowardKeepsItsHistory)
        {
            const SceneDesc scene = paneUnderLamps().mScene;
            const Shaders::VisibilityConstants before = darkEyeAt(osg::Vec3f());
            Shaders::VisibilityConstants after = darkEyeAt(osg::Vec3f(0.0f, 10.0f, 0.0f));

            const Frame averaged = shoot(scene, {}, after, sSize, { .mFrames = 16, .mFirstFrame = 3000 });

            after.mFrame = 2016;
            const Frame raw = shoot(scene, {}, after, sSize);

            shoot(scene, {}, before, sSize, filteredRun(16, 2000));
            const Frame filtered = shoot(scene, {}, after, sSize, { .mFilter = true, .mSetScene = false });

            std::vector<float> paneMotion;
            mRenderer.readChannel(Channel::PaneMotion, paneMotion);
            EXPECT_NEAR(paneMotion[centreOf(sSize) * 4 + 2], 9.9992f, 0.01f) << "the pane came ten units nearer";

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float rawError = raw.errorFrom(averaged, channel);
                const float filteredError = filtered.errorFrom(averaged, channel);
                EXPECT_LT(filteredError, rawError * 0.5f)
                    << "channel " << channel << ": raw " << rawError << ", filtered " << filteredError;
            }
        }

        /// **A pane is reprojected by its own motion and not by the surface's behind it.** The eye
        /// steps four units along +X past the pane 200 units ahead, the sky behind it. By
        /// `frame.cpp`'s arithmetic a point 200 units off moves `4 · 32 / tan 30° / 200` = 1.1085
        /// pixels at this size and field; the sky, at no distance a step can cross, moves nought.
        /// The pane's own channel says the first, the pixel's motion the second — and the history,
        /// fetched from where the pane stood, carries sixteen frames across the step: the filtered
        /// frame stands under half the raw frame's error from the average at the new eye. The pane is
        /// the whole of the frame's light, so the upscaler's reactive mask is how far apart the two
        /// motions stand, past `MISMOVED_FULL` and so whole; and nought for the eye standing still.
        TEST_F(RtxVisibilityTest, aPaneIsReprojectedByItsOwnMotionAndNotTheSurfacesBehindIt)
        {
            const SceneDesc scene = paneUnderLamps().mScene;
            const Shaders::VisibilityConstants before = darkEyeAt(osg::Vec3f());
            Shaders::VisibilityConstants after = darkEyeAt(osg::Vec3f(4.0f, 0.0f, 0.0f));

            const Frame averaged = shoot(scene, {}, after, sSize, { .mFrames = 16, .mFirstFrame = 3000 });

            after.mFrame = 2016;
            const Frame raw = shoot(scene, {}, after, sSize);

            // Jittered, as every frame an upscaler takes is: the jitter is no motion, and must mark
            // nothing.
            Shot still = filteredRun(16, 2000);
            still.mJitter = true;
            shoot(scene, {}, before, sSize, still);
            const std::size_t centre = centreOf(sSize);
            std::vector<float> masks;
            mRenderer.readChannel(Channel::UpscaleMasks, masks);
            EXPECT_EQ(*std::max_element(masks.begin(), masks.end()), 0.0f)
                << "an eye standing still moves nothing apart";

            const Frame filtered = shoot(scene, {}, after, sSize, { .mFilter = true, .mSetScene = false });
            mRenderer.readChannel(Channel::UpscaleMasks, masks);
            EXPECT_EQ(masks[centre * 2], 1.0f) << "the pane moved a pixel apart from the sky behind it";
            EXPECT_EQ(masks[centre * 2 + 1], 0.0f) << "no water stands anywhere";

            std::vector<float> paneMotion;
            mRenderer.readChannel(Channel::PaneMotion, paneMotion);
            std::vector<float> motion;
            mRenderer.readChannel(Channel::Motion, motion);
            EXPECT_NEAR(paneMotion[centre * 4], 1.1085f, 0.02f) << "the pane's own step";
            EXPECT_NEAR(paneMotion[centre * 4 + 1], 0.0f, 0.02f);
            EXPECT_NEAR(motion[centre * 4], 0.0f, 0.02f) << "the sky behind it";

            for (std::size_t channel = 0; channel < 3; ++channel)
            {
                const float rawError = raw.errorFrom(averaged, channel);
                const float filteredError = filtered.errorFrom(averaged, channel);
                EXPECT_LT(filteredError, rawError * 0.5f)
                    << "channel " << channel << ": raw " << rawError << ", filtered " << filteredError;
            }
        }
    }
}
