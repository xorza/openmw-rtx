#ifndef OPENMW_APPS_RTXTOOL_PLACEMENT_H
#define OPENMW_APPS_RTXTOOL_PLACEMENT_H

#include <optional>
#include <string_view>

#include <osg/BoundingBox>
#include <osg/Vec3f>

namespace RtxTool
{
    /// Where the camera ends up once whatever was left unsaid has been filled in.
    struct Placement
    {
        osg::Vec3f mOrigin;
        osg::Vec3f mTarget;
    };

    /// Fills in whichever of origin and target was not given, with a view of the whole scene from
    /// outside it.
    ///
    /// Shared by the window and the screenshot so that asking for a picture of what you are looking
    /// at gives you a picture of what you are looking at. It is the one placement that needs nothing
    /// known about the cell, and a poor view of an interior, whose walls stand between the camera
    /// and everything worth seeing — name coordinates for those.
    Placement placeCamera(const osg::BoundingBoxf& bounds, float verticalFovDegrees,
        const std::optional<osg::Vec3f>& origin, const std::optional<osg::Vec3f>& target);

    /// Parses `x,y,z`. Empty text is not a failure; it means nothing was said.
    ///
    /// Throws `std::runtime_error` naming `what` when the text is present and malformed.
    std::optional<osg::Vec3f> parseVec3(std::string_view text, std::string_view what);
}

#endif
