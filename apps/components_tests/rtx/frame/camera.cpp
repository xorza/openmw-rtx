#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <osg/Matrixd>
#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3d>
#include <osg/Vec3f>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/testcamera.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/visibility.h>

namespace Rtx
{
    namespace
    {
        TEST(RtxCameraTest, theBasisIsRightHandedAboutTheWorldsUpAxis)
        {
            // Looking along +Y from the origin, 90 degrees of vertical field of view, square image:
            // the half-extents at unit distance are both tan(45) = 1.
            const Shaders::VisibilityConstants camera = Testing::makeCamera(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 90.0f, 100, 100, 1000.0f);

            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mForward.y(), 1.0f, 1e-5f);
            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mRight.x(), 1.0f, 1e-5f);
            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mUp.z(), 1.0f, 1e-5f);
        }

        TEST(RtxCameraTest, aWiderImageWidensTheHorizontalExtentAndLeavesTheVerticalAlone)
        {
            const Shaders::VisibilityConstants wide = Testing::makeCamera(
                osg::Vec3f(0.0f, 0.0f, 0.0f), osg::Vec3f(0.0f, 1.0f, 0.0f), 90.0f, 200, 100, 1000.0f);

            EXPECT_NEAR(wide.mEyes.mWorld.mBasis.mRight.x(), 2.0f, 1e-5f);
            EXPECT_NEAR(wide.mEyes.mWorld.mBasis.mUp.z(), 1.0f, 1e-5f);
        }

        /// **A turn of the head moves no camera**, however far out the eye stands. A view matrix's
        /// translation is `-R t`, and at `|t|` near 10^5 a float ulp is about 0.008: an inverse
        /// taken in float puts the eye back with an error that changes with `R` alone, which the
        /// motion between two frames reads as a step. Taken in double, every turn of one eye
        /// stands the camera at the float nearest that eye — and the float inverse of the same
        /// views does not, which is what makes this a test of the precision and not of the turns.
        TEST(RtxCameraTest, aTurnOfTheHeadMovesNoCamera)
        {
            const osg::Vec3d eye(98765.4321, -87654.321, 1234.5);
            const osg::Vec3f nearest(eye);

            bool floatMoved = false;
            for (int step = 0; step < 16; ++step)
            {
                const double heading = step * 0.37;
                const double pitch = (step % 5 - 2) * 0.3;
                const osg::Vec3d look(
                    std::cos(pitch) * std::sin(heading), std::cos(pitch) * std::cos(heading), std::sin(pitch));
                const osg::Matrixd view = osg::Matrixd::lookAt(eye, eye + look, osg::Vec3d(0.0, 0.0, 1.0));

                const Viewpoint camera = makeCameraFromView(view, 60.0f, 64, 64, sNearPlane, 1000.0f).value();
                EXPECT_EQ(camera.mOrigin, nearest) << "the eye moved at turn " << step;
                EXPECT_EQ(makeOrthographicCameraFromView(view, 200.0f, 200.0f, 64, 64, 1.0f, 1000.0f)->mOrigin, nearest)
                    << "the box's eye moved at turn " << step;

                floatMoved = floatMoved || osg::Vec3f(osg::Matrixf::inverse(osg::Matrixf(view)).getTrans()) != nearest;
            }
            EXPECT_TRUE(floatMoved) << "no turn moved a float inverse, so nothing here tests the precision";
        }

        /// A view with no basis is nothing rather than a camera of NaN: a camera nobody filled in
        /// arrives every frame, and a frame skips rather than filling the image with NaN and
        /// reporting nothing. An eye looking at itself inverts to no matrix, and one looking
        /// straight down with the world's up for its roll has no right-hand side.
        TEST(RtxCameraTest, aViewWithNoBasisIsNothingRatherThanNaN)
        {
            const osg::Vec3f eye(1.0f, 2.0f, 3.0f);
            const osg::Vec3f up(0.0f, 0.0f, 1.0f);
            EXPECT_FALSE(
                makeCameraFromView(osg::Matrixf::lookAt(eye, eye, up), 60.0f, 64, 64, sNearPlane, 1.0f).has_value());

            const osg::Vec3f above(0.0f, 0.0f, 100.0f);
            EXPECT_FALSE(
                makeCameraFromView(osg::Matrixf::lookAt(above, osg::Vec3f(), up), 60.0f, 64, 64, sNearPlane, 1.0f)
                    .has_value());

            // **Nor at an angle that is no field of view**, which a script's override or the
            // content's werewolf one can be: nought spreads no rays, 180 stands the image plane at
            // infinity, and past it the picture turns over. Either side of each edge.
            const osg::Matrixf ahead = osg::Matrixf::lookAt(eye, eye + osg::Vec3f(0.0f, 1.0f, 0.0f), up);
            for (const float none : { 0.0f, -10.0f, 180.0f, 200.0f, std::numeric_limits<float>::quiet_NaN() })
            {
                EXPECT_FALSE(isFieldOfView(none)) << none;
                EXPECT_FALSE(makeCameraFromView(ahead, none, 64, 64, sNearPlane, 1.0f).has_value()) << none;
            }
            const Viewpoint seeing = makeCameraFromView(ahead, 60.0f, 64, 64, sNearPlane, 1.0f).value();
            EXPECT_FALSE(cameraAtFieldOfView(seeing.mEyes.mWorld, 0.0f).has_value());
            EXPECT_FALSE(cameraAtFieldOfView(seeing.mEyes.mWorld, 200.0f).has_value());
            EXPECT_TRUE(cameraAtFieldOfView(seeing.mEyes.mWorld, 0.5f).has_value());
            EXPECT_TRUE(cameraAtFieldOfView(seeing.mEyes.mWorld, 179.5f).has_value());
        }

        /// Straight down, the one viewpoint a map has, with the roll `lookAt`'s own up gives it.
        ///
        /// The extents are the box in world units and not an angle: half of two hundred across and
        /// half of a hundred down, on the axes `lookAt` puts them.
        TEST(RtxCameraTest, anOrthographicCameraCarriesItsBoxRatherThanAFieldOfView)
        {
            const osg::Matrixf view
                = osg::Matrixf::lookAt(osg::Vec3f(0.0f, 0.0f, 100.0f), osg::Vec3f(), osg::Vec3f(0.0f, 1.0f, 0.0f));

            const Viewpoint camera = makeOrthographicCameraFromView(view, 200.0f, 100.0f, 64, 32, 5.0f, 400.0f).value();

            EXPECT_EQ(camera.mEyes.mWorld.mOrthographic, 1u);

            EXPECT_NEAR(camera.mOrigin.z(), 100.0f, 1e-4f);
            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mForward.z(), -1.0f, 1e-5f);
            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mRight.x(), 100.0f, 1e-4f);
            EXPECT_NEAR(camera.mEyes.mWorld.mBasis.mUp.y(), 50.0f, 1e-4f);

            // No angle, because a parallel ray's cone does not widen; the shader takes the pixel's
            // constant footprint off `mRight` instead.
            EXPECT_EQ(camera.mEyes.mWorld.mSpreadAngle, 0.0f);

            // **A point of the picture is looked through along the forward, from where the box puts
            // it**, which a pick reads as the trace does (`rayAcross`). A quarter of the way right
            // of the centre and a quarter up is half of `mRight` and half of `mUp`: 50 across and
            // 25 up, and the ray goes straight down the view.
            const Shaders::Ray parallel = Shaders::rayAcross(camera.mEyes.mWorld, osg::Vec2f(0.5f, -0.5f));
            EXPECT_NEAR(parallel.mOffset.x(), 50.0f, 1e-4f);
            EXPECT_NEAR(parallel.mOffset.y(), 25.0f, 1e-4f);
            EXPECT_NEAR(parallel.mOffset.z(), 0.0f, 1e-4f);
            EXPECT_NEAR(parallel.mDirection.z(), -1.0f, 1e-6f);

            // And under a pinhole the same offset turns the direction and moves no origin: the
            // plane's right edge of a unit basis is 45 degrees off the forward.
            Shaders::Camera pinhole = camera.mEyes.mWorld;
            pinhole.mOrthographic = 0u;
            pinhole.mBasis.mForward = osg::Vec3f(0.0f, 0.0f, -1.0f);
            pinhole.mBasis.mRight = osg::Vec3f(1.0f, 0.0f, 0.0f);
            pinhole.mBasis.mUp = osg::Vec3f(0.0f, 1.0f, 0.0f);
            const Shaders::Ray fanned = Shaders::rayAcross(pinhole, osg::Vec2f(1.0f, 0.0f));
            EXPECT_EQ(fanned.mOffset, osg::Vec3f());
            EXPECT_NEAR(fanned.mDirection.x(), std::sqrt(0.5f), 1e-6f);
            EXPECT_NEAR(fanned.mDirection.z(), -std::sqrt(0.5f), 1e-6f);

            // **A shifted picture looks ahead where the shift put the axis.** Half the picture right
            // and a quarter up, in clip units, is half right and a quarter *up* the picture, whose y
            // runs down: the forward is seen there, and the 45 degrees one unit right of it. Through
            // the arms' plane twice as wide, half a unit right of the axis is that same edge.
            Shaders::Camera shifted = pinhole;
            shiftPicture(shifted, osg::Vec2f(0.5f, 0.25f));
            EXPECT_EQ(shifted.mBasis.mCentre, osg::Vec2f(0.5f, -0.25f));
            EXPECT_EQ(Shaders::rayAcross(shifted, osg::Vec2f(0.5f, -0.25f)).mDirection, pinhole.mBasis.mForward);
            const osg::Vec3f edge = Shaders::rayAcross(shifted, osg::Vec2f(1.5f, -0.25f)).mDirection;
            EXPECT_EQ(edge, fanned.mDirection);
            EXPECT_EQ(Shaders::directionAcross(shifted.mBasis, osg::Vec2f(1.0f, -0.25f), osg::Vec2f(2.0f, 1.0f)),
                fanned.mDirection);

            // A parallel picture's box moves the same way.
            Shaders::Camera slid = camera.mEyes.mWorld;
            shiftPicture(slid, osg::Vec2f(0.5f, 0.25f));
            EXPECT_EQ(Shaders::rayAcross(slid, osg::Vec2f(0.5f, -0.25f)).mOffset, osg::Vec3f());

            Testing::expectDies([&] { makeOrthographicCameraFromView(view, 0.0f, 100.0f, 64, 32, 5.0f, 400.0f); },
                "an orthographic camera with no extent sees nothing");
        }

        /// A basis read the other way: the right over its squared length, 2 / 4 = 0.5 along x, and
        /// the up over its own turned down the image, -0.5 / 0.25 = -2 along z, with the forward and
        /// the centre as they were. The ray through `uv = centre + (1, -1)` passes `forward + right +
        /// up` one unit ahead, and that point lands back on its `uv`: 2 * 0.5 + 0.25 = 1.25 across and
        /// 0.5 * -2 - 0.5 = -1.5 down. A basis of nought, a frame with no eye before it, reads as
        /// nought.
        TEST(RtxCameraTest, aScreenBasisIsTheBasisOverItsOwnSquares)
        {
            const Shaders::Basis basis{
                .mForward = osg::Vec3f(0.0f, 1.0f, 0.0f),
                .mRight = osg::Vec3f(2.0f, 0.0f, 0.0f),
                .mUp = osg::Vec3f(0.0f, 0.0f, 0.5f),
                .mCentre = osg::Vec2f(0.25f, -0.5f),
            };

            const Shaders::ScreenBasis screen = screenBasisOf(basis);
            EXPECT_EQ(screen.mForward, basis.mForward);
            EXPECT_EQ(screen.mAcross, osg::Vec3f(0.5f, 0.0f, 0.0f));
            EXPECT_EQ(screen.mDown, osg::Vec3f(0.0f, 0.0f, -2.0f));
            EXPECT_EQ(screen.mCentre, basis.mCentre);

            const osg::Vec3f offset = basis.mForward + basis.mRight + basis.mUp;
            const float ahead = offset * screen.mForward;
            EXPECT_EQ(ahead, 1.0f);
            EXPECT_EQ(offset * screen.mAcross + screen.mCentre.x() * ahead, 1.25f);
            EXPECT_EQ(offset * screen.mDown + screen.mCentre.y() * ahead, -1.5f);

            const Shaders::ScreenBasis none = screenBasisOf(Shaders::Basis{
                .mForward = osg::Vec3f(), .mRight = osg::Vec3f(), .mUp = osg::Vec3f(), .mCentre = osg::Vec2f() });
            EXPECT_EQ(none.mForward, osg::Vec3f());
            EXPECT_EQ(none.mAcross, osg::Vec3f());
            EXPECT_EQ(none.mDown, osg::Vec3f());
        }

        /// **A viewpoint's block describes no world, whichever builder made the viewpoint.** What
        /// `constantsFor` lays the viewpoint over, before anything has described the world, has to
        /// be one answer for `describeWorld` to overwrite — a sea level of never, a heading the
        /// tiles were drawn on, and a fog layer of the height `FOG_HEIGHT` names — and the viewpoint
        /// itself has to arrive whole.
        ///
        /// **The clip is the caller's and the reach is the world's.** A picture that clips at four
        /// hundred units still sends its shadow and ambient rays to `sFarPlane`, because what
        /// lights a point is the world around it and not how near a picture of it stops.
        TEST(RtxCameraTest, aViewpointsBlockDescribesNoWorldWhicheverBuilderMadeIt)
        {
            const osg::Vec3f eye(0.0f, 0.0f, 100.0f);
            const osg::Matrixf view = osg::Matrixf::lookAt(eye, osg::Vec3f(), osg::Vec3f(0.0f, 1.0f, 0.0f));

            const std::array cameras{
                makeCameraFromView(view, 60.0f, 64, 32, 1.0f, 400.0f).value(),
                makeOrthographicCameraFromView(view, 200.0f, 100.0f, 64, 32, 1.0f, 400.0f).value(),
            };

            for (const Viewpoint& viewpoint : cameras)
            {
                const Shaders::VisibilityConstants camera = constantsFor(viewpoint);
                EXPECT_EQ(camera.mOrigin, viewpoint.mOrigin);
                EXPECT_EQ(camera.mEyes.mWorld.mBasis.mRight, viewpoint.mEyes.mWorld.mBasis.mRight);
                EXPECT_EQ(camera.mEyes.mArms.mWidth, viewpoint.mEyes.mArms.mWidth);
                EXPECT_EQ(camera.mWaterLevel, -std::numeric_limits<float>::infinity());
                // The rasterizer's fixed wind, `(0.5, -0.8)` over its length 0.943398, which is
                // `(0.529999, -0.847998)`: no world turns it.
                EXPECT_FLOAT_EQ(camera.mSeaHeading.x(), 0.5f / std::sqrt(0.89f));
                EXPECT_FLOAT_EQ(camera.mSeaHeading.y(), -0.8f / std::sqrt(0.89f));
                EXPECT_EQ(camera.mFogLift, 1.0f);
                EXPECT_EQ(camera.mStars.mTexture, Shaders::NO_TEXTURE) << "a star sheet named before a world";
                EXPECT_EQ(camera.mFar, 400.0f);
                EXPECT_EQ(camera.mReach, sFarPlane);
                EXPECT_EQ(camera.mNear, 1.0f);
            }
        }

        /// **The image plane is the field of view over the extent.** Hand-computed at 90 degrees
        /// over 200 by 100: the half-height is `tan(45°)` — one — the half-width is that times the
        /// aspect, which is two, and one pixel covers `atan(2 / 100)` radians.
        TEST(RtxCameraTest, theImagePlaneIsTheFieldOfViewOverTheExtent)
        {
            const osg::Vec3f eye(3.0f, 4.0f, 5.0f);
            const osg::Matrixf view
                = osg::Matrixf::lookAt(eye, eye + osg::Vec3f(0.0f, 1.0f, 0.0f), osg::Vec3f(0.0f, 0.0f, 1.0f));
            const Viewpoint viewed = makeCameraFromView(view, 90.0f, 200, 100, 1.0f, 1000.0f).value();

            EXPECT_NEAR(viewed.mEyes.mWorld.mBasis.mRight.length(), 2.0f, 1e-5f);
            EXPECT_NEAR(viewed.mEyes.mWorld.mBasis.mUp.length(), 1.0f, 1e-5f);
            EXPECT_NEAR(viewed.mEyes.mWorld.mSpreadAngle, std::atan(2.0f / 100.0f), 1e-6f);
        }

        /// The arms' eye is the eye's own until something widens it, and widening keeps the basis
        /// and moves the plane.
        ///
        /// Ninety degrees over 200 by 100 is the plane `theImagePlaneIsTheFieldOfViewOverTheExtent`
        /// works out — half-height one, half-width two, `atan(2 / 100)` a pixel — reached here
        /// from a sixty-degree camera whose own half-height is `tan(30°)`.
        TEST(RtxCameraTest, theArmsEyeIsTheEyesOwnUntilWidened)
        {
            const osg::Vec3f eye(3.0f, 4.0f, 5.0f);
            const osg::Vec3f along(0.0f, 1.0f, 0.0f);
            const osg::Matrixf view = osg::Matrixf::lookAt(eye, eye + along, osg::Vec3f(0.0f, 0.0f, 1.0f));

            for (const Viewpoint& built : { makeCameraFromView(view, 60.0f, 200, 100, 1.0f, 1000.0f).value(),
                     makeOrthographicCameraFromView(view, 200.0f, 100.0f, 200, 100, 1.0f, 1000.0f).value() })
            {
                EXPECT_EQ(built.mEyes.mArms.mBasis.mForward, built.mEyes.mWorld.mBasis.mForward);
                EXPECT_EQ(built.mEyes.mArms.mBasis.mRight, built.mEyes.mWorld.mBasis.mRight);
                EXPECT_EQ(built.mEyes.mArms.mBasis.mUp, built.mEyes.mWorld.mBasis.mUp);
                EXPECT_EQ(built.mEyes.mArms.mSpreadAngle, built.mEyes.mWorld.mSpreadAngle);
                EXPECT_EQ(built.mEyes.mArms.mWidth, built.mEyes.mWorld.mWidth);
            }

            const Viewpoint narrow = makeCameraFromView(view, 60.0f, 200, 100, 1.0f, 1000.0f).value();
            const Shaders::Camera wide = cameraAtFieldOfView(narrow.mEyes.mWorld, 90.0f).value();

            EXPECT_EQ(wide.mBasis.mForward, narrow.mEyes.mWorld.mBasis.mForward);
            EXPECT_NEAR(wide.mBasis.mRight.length(), 2.0f, 1e-5f);
            EXPECT_NEAR(wide.mBasis.mUp.length(), 1.0f, 1e-5f);
            EXPECT_NEAR(wide.mSpreadAngle, std::atan(2.0f / 100.0f), 1e-6f);
            EXPECT_EQ(wide.mWidth, 200u);
            EXPECT_EQ(wide.mHeight, 100u);

            // The same axes, only longer.
            for (int axis = 0; axis < 3; ++axis)
            {
                EXPECT_NEAR(wide.mBasis.mRight[axis] / wide.mBasis.mRight.length(),
                    narrow.mEyes.mWorld.mBasis.mRight[axis] / narrow.mEyes.mWorld.mBasis.mRight.length(), 1e-6f)
                    << "right " << axis;
                EXPECT_NEAR(wide.mBasis.mUp[axis] / wide.mBasis.mUp.length(),
                    narrow.mEyes.mWorld.mBasis.mUp[axis] / narrow.mEyes.mWorld.mBasis.mUp.length(), 1e-6f)
                    << "up " << axis;
            }
        }

        /// Halton, against its own definition worked out by hand.
        ///
        /// The radical inverse writes an index in a base and reflects its digits about the point, so
        /// term one in base two is 0.1 binary and term two is 0.01 — a half and a quarter. Base
        /// three's first three are a third, two thirds and a ninth. Centring subtracts a half from
        /// each, and the sequence is counted from one because term zero is the origin: a frame that
        /// sampled the pixel's corner would tell an upscaler nothing an unjittered one did not.
        TEST(RtxJitterTest, theSequenceIsHaltonInTwoAndThreeAndStraddlesTheCentre)
        {
            EXPECT_NEAR(haltonJitter(0).x(), 0.0f, 1e-6f) << "1/2 - 1/2";
            EXPECT_NEAR(haltonJitter(1).x(), -0.25f, 1e-6f) << "1/4 - 1/2";
            EXPECT_NEAR(haltonJitter(2).x(), 0.25f, 1e-6f) << "3/4 - 1/2";
            EXPECT_NEAR(haltonJitter(3).x(), -0.375f, 1e-6f) << "1/8 - 1/2";

            EXPECT_NEAR(haltonJitter(0).y(), 1.0f / 3.0f - 0.5f, 1e-6f);
            EXPECT_NEAR(haltonJitter(1).y(), 2.0f / 3.0f - 0.5f, 1e-6f);
            EXPECT_NEAR(haltonJitter(2).y(), 1.0f / 9.0f - 0.5f, 1e-6f);

            // Inside the pixel, every term, which is what makes it a sub-pixel offset rather than a
            // camera shake.
            for (std::uint32_t index = 0; index < 64; ++index)
            {
                const osg::Vec2f at = haltonJitter(index);
                EXPECT_GE(at.x(), -0.5f);
                EXPECT_LT(at.x(), 0.5f);
                EXPECT_GE(at.y(), -0.5f);
                EXPECT_LT(at.y(), 0.5f);
            }
        }
    }
}
