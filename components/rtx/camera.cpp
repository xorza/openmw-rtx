#include "camera.hpp"

#include <cassert>
#include <cmath>
#include <limits>

#include <osg/Math>

#include "error.hpp"

namespace Rtx
{
    Shaders::VisibilityConstants makeCamera(const osg::Vec3f& origin, const osg::Vec3f& target,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far)
    {
        assert(width > 0 && height > 0);

        osg::Vec3f forward = target - origin;
        if (forward.length2() <= 0.0f)
            throw Error("the camera is standing where it is looking");
        forward.normalize();

        const osg::Vec3f worldUp(0.0f, 0.0f, 1.0f);
        osg::Vec3f right = forward ^ worldUp;
        if (right.length2() <= 1e-6f)
            throw Error("the camera looks along the world's up axis, which leaves its roll undefined");
        right.normalize();

        osg::Vec3f up = right ^ forward;
        up.normalize();

        const float halfHeight = std::tan(osg::DegreesToRadians(verticalFovDegrees) * 0.5f);
        const float halfWidth = halfHeight * static_cast<float>(width) / static_cast<float>(height);

        // The vertical angle one pixel covers. Pixels are square here, so one number does for both.
        const float spread = std::atan(2.0f * halfHeight / static_cast<float>(height));

        return Shaders::VisibilityConstants{
            .mOrigin = origin,
            .mForward = forward,
            .mRight = right * halfWidth,
            .mUp = up * halfHeight,
            .mWidth = width,
            .mHeight = height,
            .mFar = far,
            .mSpreadAngle = spread,
            // Not zero, which would be sea level: a world with no water has to answer "how deep is
            // this point" with never, and only an infinity does that without a second question.
            .mWaterLevel = -std::numeric_limits<float>::infinity(),
        };
    }
}
