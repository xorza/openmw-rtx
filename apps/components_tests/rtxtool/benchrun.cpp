#include <span>

#include <gtest/gtest.h>

#include <osg/Math>
#include <osg/Vec3f>

#include <apps/rtxtool/model/benchrun.hpp>
#include <components/rtx/frame/reconstruction.hpp>

namespace RtxTool
{
    namespace
    {
        /// The rotation a stand hands the game's body, in the game's own angles.
        TEST(RtxBenchRunTest, aStandsRotationIsTheBodysYawClockwiseFromNorthAndPitchNegativeUp)
        {
            const osg::Vec3f eye(10.0f, 20.0f, 30.0f);

            const Stand north{ .mEye = eye, .mLook = eye + osg::Vec3f(0.0f, 100.0f, 0.0f) };
            EXPECT_EQ(north.getRotation(), osg::Vec3f(0.0f, 0.0f, 0.0f));

            const Stand east{ .mEye = eye, .mLook = eye + osg::Vec3f(100.0f, 0.0f, 0.0f) };
            EXPECT_NEAR(east.getRotation().z(), osg::PI_2, 1e-6);
            EXPECT_NEAR(east.getRotation().x(), 0.0f, 1e-6f);

            const Stand up{ .mEye = eye, .mLook = eye + osg::Vec3f(0.0f, 0.0f, 100.0f) };
            EXPECT_NEAR(up.getRotation().x(), -osg::PI_2, 1e-6);

            // A stand with no look faces north, as `getLook` says.
            Stand bare{ .mEye = eye };
            EXPECT_EQ(bare.getRotation(), osg::Vec3f(0.0f, 0.0f, 0.0f));

            // A look is taken over an eye and refused without one, where it would be dropped.
            EXPECT_TRUE(bare.lookAt(eye + osg::Vec3f(100.0f, 0.0f, 0.0f)));
            EXPECT_NEAR(bare.getRotation().z(), osg::PI_2, 1e-6);
            Stand blind{ .mCell = "-2,-9" };
            EXPECT_FALSE(blind.lookAt(eye));
            EXPECT_FALSE(blind.mLook.has_value());
        }

        /// The ship at Seyda Neen, by hand: forward (-4411, 2767, -120) is 5208.4 long, so the yaw
        /// is atan2(-4411, 2767) = -1.01055 rad (302.1° from north) and the pitch is
        /// -asin(-120 / 5208.4) = +0.02304 rad, a climb of -1.32°.
        TEST(RtxBenchRunTest, theShipsStandFacesTheTownAndDipsALittle)
        {
            const Stand ship{
                .mEye = osg::Vec3f(-8292.0f, -73376.0f, 320.0f),
                .mLook = osg::Vec3f(-12703.0f, -70609.0f, 200.0f),
            };
            const osg::Vec3f rotation = ship.getRotation();

            EXPECT_NEAR(rotation.z(), -1.01055f, 1e-4f);
            EXPECT_NEAR(rotation.x(), 0.02304f, 1e-4f);
            EXPECT_EQ(rotation.y(), 0.0f);
        }

        /// What `CameraDriver::standWhereThePlayerIs` rebuilds from a body's rotation is the
        /// stand that rotated it. The ship again, so the two are checked on the same numbers:
        /// forward is (-4411, 2767, -120) / 5208.4 = (-0.8469, 0.5313, -0.0230).
        TEST(RtxBenchRunTest, aBodysRotationReadsBackAsTheStandThatMadeIt)
        {
            const Stand stand{
                .mEye = osg::Vec3f(-8292.0f, -73376.0f, 320.0f),
                .mLook = osg::Vec3f(-12703.0f, -70609.0f, 200.0f),
            };
            const osg::Vec3f forward = Stand::forwardOf(stand.getRotation());

            EXPECT_NEAR(forward.x(), -0.8469f, 1e-4f);
            EXPECT_NEAR(forward.y(), 0.5313f, 1e-4f);
            EXPECT_NEAR(forward.z(), -0.0230f, 1e-4f);
            EXPECT_NEAR(forward.length(), 1.0f, 1e-6f);

            EXPECT_EQ(Stand::forwardOf(osg::Vec3f()), osg::Vec3f(0.0f, 1.0f, 0.0f));
        }

        /// **A strafe flies in from the stand's left and arrives on the last frame.** 150 units over
        /// thirty frames of a sixtieth of a second: twenty-nine steps, so 150 · 60 / 29 = 310.345
        /// units a second. Facing north, the left is west; facing east, it is north.
        TEST(RtxBenchRunTest, aStrafeFliesInFromTheStandsLeftAndArrivesOnTheLastFrame)
        {
            const Stand north{ .mCell = "Balmora",
                .mEye = osg::Vec3f(100.0f, 200.0f, 300.0f),
                .mLook = osg::Vec3f(100.0f, 1200.0f, 300.0f) };
            const Approach fromWest = north.approachFromSide(150.0f, 1.0f / 60.0f, 30);
            EXPECT_EQ(fromWest.mFrom.mCell, "Balmora");
            EXPECT_NEAR(fromWest.mFrom.mEye->x(), -50.0f, 1e-4f);
            EXPECT_NEAR(fromWest.mFrom.mEye->y(), 200.0f, 1e-4f);
            EXPECT_NEAR(fromWest.mFrom.mEye->z(), 300.0f, 1e-4f);
            EXPECT_EQ(fromWest.mFrom.mLook, north.mLook) << "it faces the stand's own point all the way";
            EXPECT_EQ(fromWest.mRoute.mTo, *north.mEye);
            EXPECT_EQ(fromWest.mRoute.mLookTo, *north.mLook);
            EXPECT_NEAR(fromWest.mRoute.mSpeed, 310.345f, 1e-3f);
            EXPECT_TRUE(fromWest.mRoute.mWorldHeld) << "the world the reference stands in, and not a walk";

            const Stand east{ .mEye = osg::Vec3f(100.0f, 200.0f, 300.0f),
                .mLook = osg::Vec3f(1100.0f, 200.0f, 300.0f) };
            const osg::Vec3f fromNorth = *east.approachFromSide(150.0f, 1.0f / 60.0f, 30).mFrom.mEye;
            EXPECT_NEAR(fromNorth.x(), 100.0f, 1e-4f);
            EXPECT_NEAR(fromNorth.y(), 350.0f, 1e-4f);

            // Twice as far in the same frames is twice as fast: the distance matters.
            EXPECT_NEAR(north.approachFromSide(300.0f, 1.0f / 60.0f, 30).mRoute.mSpeed, 620.690f, 1e-3f);
        }

        /// **Every check has a row, and the row says when it may be asked.** A check with no name
        /// would print empty in the report and be unreachable from the command line; the four that
        /// depend on the stop's shape answer no where the stop cannot answer them.
        TEST(RtxBenchRunTest, everyCheckIsNamedAndSaysWhenItMayBeAsked)
        {
            const std::span<const Check> every = everyCheck();
            EXPECT_EQ(every.size(), 13u);
            for (const Check check : every)
                EXPECT_FALSE(checkName(check).empty()) << static_cast<int>(check);

            Stop still;
            still.mStand.mEye = osg::Vec3f(1.0f, 2.0f, 3.0f);
            Rtx::RenderProfile unheld;
            Rtx::RenderProfile held = unheld;
            held.mStressOverlapMs = 8.0;

            EXPECT_TRUE(canAsk(Check::WalkTwice, still, unheld));
            EXPECT_TRUE(canAsk(Check::CameraStands, still, unheld)) << "a still stop names its eye";
            EXPECT_FALSE(canAsk(Check::CrossingsAppend, still, unheld)) << "nothing to cross without a route";
            EXPECT_TRUE(canAsk(Check::FramesOverlap, still, unheld));
            EXPECT_FALSE(canAsk(Check::QueueHeld, still, unheld)) << "a hold nobody asked for";
            EXPECT_TRUE(canAsk(Check::QueueHeld, still, held));
            EXPECT_TRUE(canAsk(Check::Finite, still, unheld)) << "every frame counts what it wrote";

            Stop routed = still;
            routed.mSchedule.mRoute = Route{ .mTo = osg::Vec3f(100.0f, 0.0f, 0.0f), .mSpeed = 10.0f };
            EXPECT_TRUE(canAsk(Check::CrossingsAppend, routed, unheld));
            EXPECT_FALSE(canAsk(Check::FramesOverlap, routed, unheld)) << "an arrival drains the ring";
            EXPECT_FALSE(canAsk(Check::CameraStands, routed, unheld)) << "a route leaves the camera elsewhere";
            EXPECT_TRUE(canAsk(Check::Finite, routed, unheld));

            Stop flown = still;
            flown.mSchedule.mFreeCamera = true;
            EXPECT_FALSE(canAsk(Check::CameraStands, flown, unheld));

            Stop unplaced;
            EXPECT_FALSE(canAsk(Check::CameraStands, unplaced, unheld)) << "no eye named, nothing to stand at";

            // The check that asks what a second walk added is what makes one: a stop that asks it
            // and not the report still walks twice.
            Actions actions;
            EXPECT_FALSE(actions.walksTwice());
            // Pushed and not assigned a braced list: GCC 13 at -O2 under the sanitizers reads the
            // one-element copy as an overread (-Wstringop-overread), a false warning the build stops on.
            actions.mChecks.push_back(Check::Finite);
            EXPECT_FALSE(actions.walksTwice());
            actions.mChecks.push_back(Check::WalkTwice);
            EXPECT_TRUE(actions.walksTwice());
            actions.mChecks.clear();
            actions.mWalkTwice = true;
            EXPECT_TRUE(actions.walksTwice());
        }
    }
}
