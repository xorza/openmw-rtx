#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <osg/Matrixd>
#include <osg/Vec3f>

#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/framepast.hpp>
#include <components/rtx/renderer/framedescriber.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    namespace
    {
        Viewpoint viewpoint()
        {
            const osg::Matrixd view = osg::Matrixd::lookAt(
                osg::Vec3f(0.0f, -10.0f, 2.0f), osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 1.0f));
            const std::optional<Viewpoint> made = makeCameraFromView(view, 60.0f, 32, 16, 1.0f, 1000.0f);
            EXPECT_TRUE(made.has_value());
            return *made;
        }

        /// **What the histories lost is folded and spent, and the first frame has no past.** The first
        /// past is everything; one taken after it holds only what the host's loss adds, a cut the
        /// reprojection and the eye and not the water, and nothing at all with no loss; a resize
        /// noted between two takes loses the reprojection alone, beside a loss of a worldspace, which
        /// loses the water too. Only a past that keeps the reprojection names a camera, the one the
        /// last trace kept.
        TEST(RtxFrameDescriberTest, aFramesPastIsWhatWasLostSinceTheLastTraceAndIsSpent)
        {
            FrameDescriber describer;
            EXPECT_FALSE(describer.getTracedFrame().has_value());

            const FramePast first = describer.takePast(HistoryLoss::None);
            EXPECT_TRUE(first.mReprojectionLost && first.mEyeLost && first.mWaterLost) << "the first frame had a past";
            EXPECT_EQ(describer.reprojectingFrom(first), nullptr);

            Shaders::VisibilityConstants stated = constantsFor(viewpoint());
            stated.mFrame = 7;
            describer.traced(stated, stated);
            EXPECT_EQ(describer.getTracedFrame(), std::optional<std::uint32_t>(7u));

            const FramePast kept = describer.takePast(HistoryLoss::None);
            EXPECT_FALSE(kept.mReprojectionLost || kept.mEyeLost || kept.mWaterLost) << "the loss was not spent";
            ASSERT_NE(describer.reprojectingFrom(kept), nullptr);
            EXPECT_EQ(describer.reprojectingFrom(kept)->mFrame, 7u);

            const FramePast cut = describer.takePast(HistoryLoss::Cut);
            EXPECT_TRUE(cut.mReprojectionLost && cut.mEyeLost);
            EXPECT_FALSE(cut.mWaterLost) << "a cut lost the wake";

            describer.lose(FramePast::resized());
            const FramePast resized = describer.takePast(HistoryLoss::None);
            EXPECT_TRUE(resized.mReprojectionLost);
            EXPECT_FALSE(resized.mEyeLost || resized.mWaterLost) << "a resize lost the eye or the wake";

            describer.lose(FramePast::resized());
            const FramePast moved = describer.takePast(HistoryLoss::Worldspace);
            EXPECT_TRUE(moved.mReprojectionLost && moved.mEyeLost && moved.mWaterLost);
        }

        /// **A picture is described off its viewpoint and its options alone**: its own sun and
        /// ambient, no shadows from the sky, the classes and the lamps it names, and a backdrop
        /// that stops where nothing was hit exactly where the clear is less than opaque.
        TEST(RtxFrameDescriberTest, aPictureIsItsViewpointUnderItsOwnLight)
        {
            GuiTraceOptions options;
            options.mClear = { 0.0f, 0.0f, 0.0f, 0.5f };
            options.mRayMask = Shaders::MASK_STATIC;
            options.mLamps = false;
            options.mLight = PictureLight{ .mDirection = osg::Vec3f(0.0f, 0.0f, 1.0f),
                .mIrradiance = osg::Vec3f(3.0f, 2.0f, 1.0f),
                .mAmbient = osg::Vec3f(0.25f, 0.5f, 0.75f) };

            const Shaders::VisibilityConstants pictured = FrameDescriber::describePicture(viewpoint(), options);
            const Shaders::SkySource sun
                = Shaders::sunSource(osg::Vec3f(0.0f, 0.0f, 1.0f), osg::Vec3f(3.0f, 2.0f, 1.0f));
            EXPECT_EQ(pictured.mSun.mDirection, sun.mDirection);
            EXPECT_EQ(pictured.mSun.mIrradiance, sun.mIrradiance);
            EXPECT_EQ(pictured.mAmbient, osg::Vec3f(0.25f, 0.5f, 0.75f));
            EXPECT_EQ(pictured.mNoSkyShadows, 1u);
            EXPECT_EQ(pictured.mTransparentBackground, 1u);
            EXPECT_EQ(pictured.mRayMask, Shaders::MASK_STATIC);
            EXPECT_EQ(pictured.mNoLamps, 1u);
            EXPECT_EQ(pictured.mEyes.mWorld.mWidth, 32u);

            options.mClear[3] = 1.0f;
            options.mLamps = true;
            const Shaders::VisibilityConstants opaque = FrameDescriber::describePicture(viewpoint(), options);
            EXPECT_EQ(opaque.mTransparentBackground, 0u) << "an opaque clear stopped where nothing was hit";
            EXPECT_EQ(opaque.mNoLamps, 0u);
        }
    }
}
