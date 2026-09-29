#include <cfloat>
#include <cmath>

#include <gtest/gtest.h>

#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtxvulkan/upscale/fsrframe.hpp>

namespace Rtx
{
    namespace
    {
        /// A pinhole eye whose image plane at unit distance is 0.75 wide and 0.5 tall either way of
        /// its centre: the tangents of its half angles.
        Shaders::Camera eyeWithJitter(const osg::Vec2f& jitter)
        {
            Shaders::Camera camera{};
            camera.mForward = osg::Vec3f(0.0f, 1.0f, 0.0f);
            camera.mRight = osg::Vec3f(0.75f, 0.0f, 0.0f);
            camera.mUp = osg::Vec3f(0.0f, 0.0f, 0.5f);
            camera.mJitter = jitter;
            return camera;
        }

        FsrFrame::Frame frameAt(VkExtent2D render, VkExtent2D output, const osg::Vec2f& jitter, bool reset = false)
        {
            return FsrFrame::Frame{
                .mRender = render,
                .mOutput = output,
                .mCamera = eyeWithJitter(jitter),
                .mDeltaMs = 16.0f,
                .mReset = reset,
            };
        }

        /// **The frame in FSR's terms, by hand.** Quality at 1920×1080 traces 1280×720: a motion
        /// vector in render pixels is scaled into UV by one over those, the downscale is 2/3 either
        /// way, the phases are eighteen, sixteen milliseconds are 0.016 s. The jitter is the trace's
        /// negated, `FsrFrame` says why. The depth factors are the reversed, infinite branch of
        /// `setupDeviceDepthToViewSpaceDepthParams` with a near plane of `sNear`: `-FLT_EPSILON`,
        /// the near plane, and the half extents' tangents.
        TEST(RtxFsrFrameTest, aFrameIsTheTracesOwnNumbersInFsrsTerms)
        {
            FsrFrame state;
            const Shaders::FsrConstants& first
                = state.advance(frameAt({ 1280, 720 }, { 1920, 1080 }, osg::Vec2f(0.25f, -0.125f)));

            EXPECT_EQ(first.mRenderSize, Shaders::ivec2(1280, 720));
            EXPECT_EQ(first.mUpscaleSize, Shaders::ivec2(1920, 1080));
            EXPECT_EQ(first.mJitter, osg::Vec2f(-0.25f, 0.125f));
            EXPECT_FLOAT_EQ(first.mMotionVectorScale.x(), 1.0f / 1280.0f);
            EXPECT_FLOAT_EQ(first.mMotionVectorScale.y(), 1.0f / 720.0f);
            EXPECT_FLOAT_EQ(first.mDownscaleFactor.x(), 2.0f / 3.0f);
            EXPECT_FLOAT_EQ(first.mDownscaleFactor.y(), 2.0f / 3.0f);
            EXPECT_EQ(first.mJitterPhases, 18.0f);
            EXPECT_FLOAT_EQ(first.mDeltaTime, 0.016f);
            EXPECT_EQ(first.mFrameIndex, 0.0f) << "the first frame has no history";
            EXPECT_FLOAT_EQ(first.mTanHalfFov, 0.75f);
            EXPECT_FLOAT_EQ(first.mViewSpaceToMetres, 1.0f / Shaders::UNITS_PER_METRE);
            EXPECT_EQ(first.mDeviceToViewDepth.x(), -FLT_EPSILON);
            EXPECT_EQ(first.mDeviceToViewDepth.y(), FsrFrame::sNear);
            EXPECT_FLOAT_EQ(first.mDeviceToViewDepth.z(), 0.75f);
            EXPECT_FLOAT_EQ(first.mDeviceToViewDepth.w(), 0.5f);
            EXPECT_FALSE(state.readsSecond()) << "the SDK's first frame reads the first of each pair";

            // The next frame carries this one's jitter and extents as the previous ones, counts one,
            // and turns to the other of each pair.
            const Shaders::FsrConstants& second
                = state.advance(frameAt({ 1280, 720 }, { 1920, 1080 }, osg::Vec2f(-0.5f, 0.5f)));
            EXPECT_EQ(second.mPreviousJitter, osg::Vec2f(-0.25f, 0.125f));
            EXPECT_EQ(second.mJitter, osg::Vec2f(0.5f, -0.5f));
            EXPECT_EQ(second.mPreviousRenderSize, Shaders::ivec2(1280, 720));
            EXPECT_EQ(second.mFrameIndex, 1.0f);
            EXPECT_TRUE(state.readsSecond());

            // A reset starts the count again and keeps what it carries.
            const Shaders::FsrConstants& reset
                = state.advance(frameAt({ 1280, 720 }, { 1920, 1080 }, osg::Vec2f(0.0f, 0.0f), true));
            EXPECT_EQ(reset.mFrameIndex, 0.0f);
            EXPECT_EQ(reset.mPreviousJitter, osg::Vec2f(0.5f, -0.5f));
        }

        /// **A reversed, infinite depth comes back as the view depth it was made from.** The
        /// callbacks write `near / z`; FSR reads `factor[1] / (depth - factor[0])`, which is `z` up to
        /// the epsilon the SDK adds so that the sky's nought is not a division by nought: a relative
        /// error of `z * FLT_EPSILON / near` at most, 2.4e-3 at twenty thousand units.
        TEST(RtxFsrFrameTest, theDepthTheCallbacksWriteIsTheViewDepthFsrReadsBack)
        {
            FsrFrame state;
            const Shaders::FsrConstants& constants
                = state.advance(frameAt({ 1920, 1080 }, { 1920, 1080 }, osg::Vec2f()));

            for (const float z : { 1.0f, 30.0f, 700.0f, 20000.0f })
            {
                const float device = FsrFrame::sNear / z;
                const float view = constants.mDeviceToViewDepth.y() / (device - constants.mDeviceToViewDepth.x());
                EXPECT_NEAR(view, z, z * (z * FLT_EPSILON / FsrFrame::sNear + 1e-6f)) << z;
            }
        }

        /// **A change of phase count walks a step a frame, and a reset takes it at once** — the SDK's
        /// rule. Quality's eighteen to performance's thirty-two at 1920 wide.
        TEST(RtxFsrFrameTest, aNewPhaseCountIsWalkedToUnlessTheHistoryIsReset)
        {
            FsrFrame state;
            state.advance(frameAt({ 1280, 720 }, { 1920, 1080 }, osg::Vec2f()));
            EXPECT_EQ(state.advance(frameAt({ 960, 540 }, { 1920, 1080 }, osg::Vec2f())).mJitterPhases, 19.0f);
            EXPECT_EQ(state.advance(frameAt({ 960, 540 }, { 1920, 1080 }, osg::Vec2f())).mJitterPhases, 20.0f);
            EXPECT_EQ(state.advance(frameAt({ 960, 540 }, { 1920, 1080 }, osg::Vec2f(), true)).mJitterPhases, 32.0f);
        }

        /// **The pyramids' dispatch is `ffxSpdSetup` over the whole frame, by hand.** A workgroup a
        /// 64-pixel tile: 1920 / 64 = 30 across and 1080 / 64 = 16.9, so 17 down, 510 in all; as many
        /// levels as the longer side halves in whole, floor(log2 1920) = 10.
        TEST(RtxFsrFrameTest, thePyramidsCoverTheFrameInSixtyFourPixelTiles)
        {
            const FsrFrame::Pyramid pyramid = FsrFrame::pyramidFor({ 1920, 1080 });
            EXPECT_EQ(pyramid.mGroupsX, 30u);
            EXPECT_EQ(pyramid.mGroupsY, 17u);
            EXPECT_EQ(pyramid.mConstants.mWorkGroups, 510u);
            EXPECT_EQ(pyramid.mConstants.mMips, 10u);
            EXPECT_EQ(pyramid.mConstants.mRenderSize, Shaders::uvec2(1920, 1080));
        }
    }
}
