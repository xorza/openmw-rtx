#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/frame/camera.hpp>
#include <components/rtx/shaders/visibility.h>

namespace Rtx::Testing
{
    /// The atmosphere a camera built by hand fades its sky by: an atmosphere whose lower ring stands
    /// on the horizon and whose upper ring is the zenith, so the fog colour runs to the sky colour
    /// over the whole hemisphere by `t = sin e / (sin e + cos e)`. Its cosine-weighted mean is a
    /// half, because `t(e) + t(90° - e) = 1` and the cosine's weight `sin 2e` is the same at both.
    inline const Shaders::SkyRamp sHemisphereRamp{
        .mBottom = 0.0f,
        .mTop = 1.0f,
        .mLow = osg::Vec2f(1.0f, 0.0f),
        .mStep = osg::Vec2f(-1.0f, 1.0f),
    };

    /// A pinhole camera at `origin` looking `along`, which need not be a unit vector, with the
    /// world's +Z up: a view matrix and `makeCameraFromView`, which is how the game builds one.
    /// Throws for a direction of nought or along the world's up, which leave no basis.
    inline Shaders::VisibilityConstants makeCameraAlong(const osg::Vec3f& origin, const osg::Vec3f& along,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far)
    {
        const osg::Matrixf view = osg::Matrixf::lookAt(origin, origin + along, osg::Vec3f(0.0f, 0.0f, 1.0f));
        const std::optional<Shaders::VisibilityConstants> camera
            = makeCameraFromView(view, verticalFovDegrees, width, height, sNearPlane, far);
        if (!camera.has_value())
            throw std::invalid_argument("a test camera with no basis to look along");

        Shaders::VisibilityConstants built = *camera;
        built.mSkyRamp = sHemisphereRamp;
        return built;
    }

    /// The same, looking at `target`.
    inline Shaders::VisibilityConstants makeCamera(const osg::Vec3f& origin, const osg::Vec3f& target,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far)
    {
        return makeCameraAlong(origin, target - origin, verticalFovDegrees, width, height, far);
    }
}
