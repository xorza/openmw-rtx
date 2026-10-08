#include "camera.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <optional>

#include <osg/Math>
#include <osg/Matrixd>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/common/radicalinverse.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/look.h>
#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    namespace
    {
        /// The half-extents of the image plane at one unit ahead, and the angle one pixel covers.
        struct Spread
        {
            float mHalfWidth = 0.0f;
            float mHalfHeight = 0.0f;

            /// The vertical angle one pixel covers. Pixels are square here, so one number does for
            /// both.
            float mAngle = 0.0f;
        };

        Spread spreadOf(float verticalFovDegrees, std::uint32_t width, std::uint32_t height)
        {
            const float halfHeight = std::tan(osg::DegreesToRadians(verticalFovDegrees) * 0.5f);

            return Spread{
                .mHalfWidth = halfHeight * static_cast<float>(width) / static_cast<float>(height),
                .mHalfHeight = halfHeight,
                .mAngle = std::atan(2.0f * halfHeight / static_cast<float>(height)),
            };
        }

        /// A frame before anything has described the world over it, with every member it does
        /// not name zero. Assigned over a zeroed record rather than written as one initialiser,
        /// which would have to name every other member: its arrays of `vec3` cannot take
        /// `RTX_ZERO`, for the reason `portable.h` gives.
        Shaders::VisibilityConstants beforeWorld()
        {
            Shaders::VisibilityConstants constants{};

            // Every bounce, until a world says the reconstruction follows the frame —
            // `VisibilityConstants::mBounceRate` says why a frame built by hand keeps them all.
            constants.mBounceRate = 1.0f;

            // No day to lift, until a world says the sun is up.
            constants.mDaylightGain = 1.0f;

            // Not zero, which would be sea level: a world with no water has to answer "how deep
            // is this point" with never, and only an infinity does that without a second
            // question.
            constants.mWaterLevel = -std::numeric_limits<float>::infinity();

            constants.mSeaHeading = Shaders::seaHeading();

            // No sheet, and not slot nought, which is a texture of whatever scene the camera is
            // traced against: a backend that measures the sheet a frame names measured that one.
            constants.mStars.mTexture = Shaders::NO_TEXTURE;

            // The layer `FOG_HEIGHT` names, until a weather says otherwise. A camera is
            // built before anything has described the air over it, and a lift of nothing is a
            // layer of no height at all rather than an absence of one. `describeWorld` overwrites
            // this with what the cell's own weather stands its fog up to.
            constants.mFogLift = 1.0f;

            // Every class, until a camera with a cull mask of its own says which it draws. The
            // harness's and the tests' cameras never do.
            constants.mRayMask = Shaders::MASK_EVERY_CLASS;
            constants.mSkyDrawn = 1;
            constants.mWaterScatter = Shaders::WATER_SCATTER_SHIPPED;

            return constants;
        }

        /// **Inverted in double**, as the walk inverts the same matrix for the same eye: the view's
        /// translation is `-R t` with `|t|` near 10^5 in an exterior, where a float inverse carries
        /// an error of a hundredth of a unit that changes with the rotation alone, and a turn of the
        /// head moved the camera.
        std::optional<ViewBasis> basisOf(const osg::Matrixd& view)
        {
            osg::Matrixd world;
            if (!world.invert(view))
                return std::nullopt;

            return viewBasisOf(world);
        }

        /// The one recipe both projections share: the eye and its axes out of `view`, the image
        /// plane's half extents and the angle a pixel covers out of `spread`.
        std::optional<Viewpoint> cameraAt(const osg::Matrixd& view, const Spread& spread, const bool orthographic,
            std::uint32_t width, std::uint32_t height, float near, float far)
        {
            assert(width > 0 && height > 0);

            const std::optional<ViewBasis> basis = basisOf(view);
            if (!basis.has_value())
                return std::nullopt;

            Viewpoint camera{ .mOrigin = basis->mOrigin, .mNear = near, .mFar = far };
            camera.mEyes.mWorld = Shaders::Camera{
                .mBasis = Shaders::Basis{
                    .mForward = basis->mForward,
                    .mRight = basis->mRight * spread.mHalfWidth,
                    .mUp = basis->mUp * spread.mHalfHeight,
                },
                .mSpreadAngle = spread.mAngle,
                .mOrthographic = orthographic ? 1u : 0u,
                .mWidth = width,
                .mHeight = height,
                .mPixelScale = Shaders::pixelScaleOf(width, height),
            };
            camera.mEyes.mArms = camera.mEyes.mWorld;

            return camera;
        }
    }

    Shaders::VisibilityConstants constantsFor(const Viewpoint& view)
    {
        Shaders::VisibilityConstants constants = beforeWorld();
        constants.mOrigin = view.mOrigin;
        constants.mNear = view.mNear;
        constants.mFar = view.mFar;
        constants.mReach = view.mReach;
        constants.mEyes = view.mEyes;
        return constants;
    }

    std::optional<ViewBasis> viewBasisOf(const osg::Matrixd& world)
    {
        // The rows of the inverse are the eye's axes written in world coordinates, and its
        // translation is where the eye stands.
        ViewBasis basis{
            .mOrigin = osg::Vec3f(world.getTrans()),
            .mForward = -osg::Vec3f(world(2, 0), world(2, 1), world(2, 2)),
            .mRight = osg::Vec3f(world(0, 0), world(0, 1), world(0, 2)),
            .mUp = osg::Vec3f(world(1, 0), world(1, 1), world(1, 2)),
        };

        // **Not `<= 0`, which NaN passes.** `osg::Matrixd::invert` inverts a singular view — an eye
        // looking at itself, or straight down along the up it was given — into NaN and says it
        // succeeded, and every axis of it then normalises to a length of NaN.
        if (!(basis.mForward.normalize() > 0.f) || !(basis.mRight.normalize() > 0.f) || !(basis.mUp.normalize() > 0.f)
            || basis.mOrigin.isNaN())
            return std::nullopt;

        return basis;
    }

    Shaders::Camera cameraAtFieldOfView(const Shaders::Camera& camera, const float verticalFovDegrees)
    {
        assert(camera.mOrthographic == 0 && "a parallel projection has no field of view to widen");

        const Spread spread = spreadOf(verticalFovDegrees, camera.mWidth, camera.mHeight);

        Shaders::Camera widened = camera;
        widened.mBasis.mRight = camera.mBasis.mRight * (spread.mHalfWidth / camera.mBasis.mRight.length());
        widened.mBasis.mUp = camera.mBasis.mUp * (spread.mHalfHeight / camera.mBasis.mUp.length());
        widened.mSpreadAngle = spread.mAngle;

        return widened;
    }

    void shiftPicture(Shaders::Camera& camera, const osg::Vec2f& shift)
    {
        // The picture's y runs down, as `rayAt` indexes it.
        camera.mBasis.mCentre = osg::Vec2f(shift.x(), -shift.y());
    }

    Shaders::ScreenBasis screenBasisOf(const Shaders::Basis& basis)
    {
        // Nought where the division would be nought by nought: a frame with no eye before it
        // carries a basis of nought, and the shader reads its forward of nought as no answer.
        const float right = basis.mRight * basis.mRight;
        const float up = basis.mUp * basis.mUp;

        return Shaders::ScreenBasis{
            .mForward = basis.mForward,
            .mAcross = right > 0.0f ? basis.mRight / right : osg::Vec3f(),
            .mDown = up > 0.0f ? -basis.mUp / up : osg::Vec3f(),
            .mCentre = basis.mCentre,
        };
    }

    std::optional<Viewpoint> makeCameraFromView(const osg::Matrixd& view, float verticalFovDegrees, std::uint32_t width,
        std::uint32_t height, float near, float far)
    {
        return cameraAt(view, spreadOf(verticalFovDegrees, width, height), false, width, height, near, far);
    }

    std::optional<Viewpoint> makeOrthographicCameraFromView(const osg::Matrixd& view, float worldWidth,
        float worldHeight, std::uint32_t width, std::uint32_t height, float near, float far)
    {
        Crash::contract(worldWidth > 0.f && worldHeight > 0.f, "an orthographic camera with no extent sees nothing");

        // A spread angle of zero, and not for want of an answer. A parallel ray's cone does not
        // widen with distance; what it has instead is a footprint one pixel of the box wide for its
        // whole length, which the shader works out from `mRight` rather than carry twice.
        return cameraAt(view,
            Spread{ .mHalfWidth = worldWidth * 0.5f, .mHalfHeight = worldHeight * 0.5f, .mAngle = 0.f }, true, width,
            height, near, far);
    }

    osg::Vec2f haltonJitter(std::uint32_t index)
    {
        // Counted from one, because the sequence's zeroth term is the origin — a frame that sampled
        // the pixel's corner would contribute nothing an unjittered frame did not.
        const std::uint32_t term = index + 1;

        // Centred, so the offsets straddle the pixel centre rather than filling the quadrant below
        // and to the right of it.
        return osg::Vec2f(radicalInverse(term, 2) - 0.5f, radicalInverse(term, 3) - 0.5f);
    }
}
