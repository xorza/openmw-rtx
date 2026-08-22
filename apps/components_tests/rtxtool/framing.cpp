#include <cmath>

#include <gtest/gtest.h>

#include <osg/Vec3f>

#include <components/rtx/camera.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/renderer.hpp>

#include <apps/rtxtool/framing.hpp>
#include <apps/rtxtool/placement.hpp>

namespace RtxTool
{
    namespace
    {
        /// A trace at 1280x720 presented at 1920x1080, which is what the harness actually runs.
        constexpr Rtx::FrameExtents sExtents{
            .mRenderWidth = 1280,
            .mRenderHeight = 720,
            .mOutputWidth = 1920,
            .mOutputHeight = 1080,
        };

        /// Out where Morrowind's cells are, because that is where the two ways of naming a direction
        /// stop agreeing to the bit.
        Placement makePlacement()
        {
            return Placement{ .mOrigin = { -19216.0f, -14896.0f, 160.0f }, .mTarget = { -19424.0f, -12960.0f, 60.0f } };
        }

        Framing makeFraming()
        {
            Framing framing = Framing::lookingFrom(makePlacement());
            framing.mFieldOfView = 60.0f;
            framing.mFar = 200000.0f;
            return framing;
        }

        /// Two points and their difference name the same camera.
        ///
        /// **The equivalence the consolidation rests on.** `shot` and `bench` held a target and
        /// called `Rtx::makeCamera`; they now hand a direction to `Rtx::makeCameraAlong` through
        /// `lookingFrom`. `makeCamera` is documented as subtracting and delegating, so this is
        /// asserting that it still does — the day it grows a step of its own, a screenshot quietly
        /// changes and nothing else says so.
        TEST(RtxFramingTest, lookingFromTwoPointsIsTheSameCameraAsAimingAtOne)
        {
            const Placement placement = makePlacement();

            const Rtx::Shaders::VisibilityConstants aimed = Rtx::makeCamera(
                placement.mOrigin, placement.mTarget, 60.0f, sExtents.mRenderWidth, sExtents.mRenderHeight, 200000.0f);
            const Rtx::Shaders::VisibilityConstants framed = makeFrameConstants(makeFraming(), sExtents);

            EXPECT_EQ(framed.mOrigin, aimed.mOrigin);
            EXPECT_EQ(framed.mForward, aimed.mForward);
            EXPECT_EQ(framed.mRight, aimed.mRight);
            EXPECT_EQ(framed.mUp, aimed.mUp);
            EXPECT_EQ(framed.mFar, aimed.mFar);
        }

        /// The trace's extent and not the presented one is what the camera is built for.
        ///
        /// A camera built for 1920x1080 and traced at 1280x720 has the wrong aspect and the wrong
        /// per-pixel ray spread, which reads as a stretched image rather than as an error.
        TEST(RtxFramingTest, theCameraIsBuiltForWhatTheTraceRunsAtRatherThanWhatIsPresented)
        {
            const Rtx::Shaders::VisibilityConstants framed = makeFrameConstants(makeFraming(), sExtents);
            const Rtx::Shaders::VisibilityConstants square = makeFrameConstants(makeFraming(),
                Rtx::FrameExtents{
                    .mRenderWidth = 720, .mRenderHeight = 720, .mOutputWidth = 1920, .mOutputHeight = 1080 });

            // 16:9 spreads the same vertical half-angle over 16/9 as much width; 1:1 does not.
            EXPECT_NEAR(framed.mRight.length() / square.mRight.length(), 16.0f / 9.0f, 1e-5f);
        }

        /// Every switch reaches the constants, and each one changes the frame.
        ///
        /// **The whole point of one block instead of three.** A field that quietly stopped being
        /// copied would leave a command rendering something else, which is exactly the drift that
        /// made `bench` the only one of the three not honouring `--albedo`.
        TEST(RtxFramingTest, eachSwitchReachesTheConstantsAndChangesThem)
        {
            const Rtx::Shaders::VisibilityConstants plain = makeFrameConstants(makeFraming(), sExtents);
            EXPECT_EQ(plain.mShowAlbedo, 0u);

            Framing shown = makeFraming();
            shown.mShowAlbedo = true;
            EXPECT_EQ(makeFrameConstants(shown, sExtents).mShowAlbedo, 1u);

            Framing lit = makeFraming();
            lit.mDelight = 0.25f;
            EXPECT_EQ(makeFrameConstants(lit, sExtents).mDelight, 0.25f);
            EXPECT_NE(makeFrameConstants(lit, sExtents).mDelight, plain.mDelight);

            Framing sequenced = makeFraming();
            sequenced.mFrame = 42;
            EXPECT_EQ(makeFrameConstants(sequenced, sExtents).mFrame, 42u);

            Framing distant = makeFraming();
            distant.mFar = 10000.0f;
            EXPECT_EQ(makeFrameConstants(distant, sExtents).mFar, 10000.0f);
            EXPECT_NE(makeFrameConstants(distant, sExtents).mFar, plain.mFar);

            // The lighting goes through `applyLighting`, so one field of it is enough to say the
            // call is made at all.
            Framing moving = makeFraming();
            moving.mLighting.mSeconds = 3.5f;
            moving.mLighting.mWaterLevel = -12.0f;
            EXPECT_EQ(makeFrameConstants(moving, sExtents).mTime, 3.5f);
            EXPECT_EQ(makeFrameConstants(moving, sExtents).mWaterLevel, -12.0f);
            EXPECT_NE(makeFrameConstants(moving, sExtents).mTime, plain.mTime);
        }

        /// A camera with no basis says so rather than filling the image with NaN.
        TEST(RtxFramingTest, aDirectionNothingCanBeBuiltFromIsRefused)
        {
            Framing nowhere = makeFraming();
            nowhere.mForward = osg::Vec3f(0.0f, 0.0f, 0.0f);
            EXPECT_THROW(makeFrameConstants(nowhere, sExtents), Rtx::Error) << "no direction to look along";

            Framing upward = makeFraming();
            upward.mForward = osg::Vec3f(0.0f, 0.0f, 1.0f);
            EXPECT_THROW(makeFrameConstants(upward, sExtents), Rtx::Error) << "straight up has no roll";

            // A viewpoint whose `pos` and `look` are the same point reaches the first of those.
            const Placement still{ .mOrigin = { 1.0f, 2.0f, 3.0f }, .mTarget = { 1.0f, 2.0f, 3.0f } };
            EXPECT_THROW(makeFrameConstants(Framing::lookingFrom(still), sExtents), Rtx::Error);
        }
    }
}
