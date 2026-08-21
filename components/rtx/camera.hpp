#pragma once

#include <cstdint>

#include <osg/Vec2f>
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

    /// Where inside its pixel frame `index` should sample, in pixels and centred on zero.
    ///
    /// **A low-discrepancy sequence and not a random one.** What an upscaler reconstructs from is
    /// the set of sub-pixel positions a few frames covered between them, so the positions have to
    /// spread evenly over the pixel rather than clump the way random draws do. Halton in bases two
    /// and three is what everything that does this uses, and its first terms are worth knowing:
    /// base two runs 1/2, 1/4, 3/4, 1/8, and base three 1/3, 2/3, 1/9.
    ///
    /// The result is in the image's axes — x right, y down — because that is what the shader adds
    /// it to.
    osg::Vec2f haltonJitter(std::uint32_t index);
}
