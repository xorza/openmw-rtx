#ifndef OPENMW_COMPONENTS_RTX_CAMERA_H
#define OPENMW_COMPONENTS_RTX_CAMERA_H

#include <cstdint>

#include <osg/Vec3f>

#include "shaders/visibility.h"

namespace Rtx
{
    /// Constants for a pinhole camera looking from `origin` towards `target`.
    ///
    /// The world's up is +Z, as Morrowind has it. A camera standing where it is looking, or pointed
    /// straight up or straight down, has no basis; both throw `Error`. These arrive from a command
    /// line, so they are input rather than a contract, and the alternative to a message is a
    /// normalised zero vector quietly filling the image with NaN.
    Shaders::VisibilityConstants makeCamera(const osg::Vec3f& origin, const osg::Vec3f& target,
        float verticalFovDegrees, std::uint32_t width, std::uint32_t height, float far);
}

#endif
