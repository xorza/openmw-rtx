#pragma once

#include <optional>

#include <osg/Vec3f>

#include "flags.hpp"

class dtNavMeshQuery;

namespace DetourNavigator
{
    struct DetourSettings;

    std::optional<osg::Vec3f> raycast(const dtNavMeshQuery& navMeshQuery, const osg::Vec3f& halfExtents,
        const osg::Vec3f& start, const osg::Vec3f& end, const Flags includeFlags);
}
