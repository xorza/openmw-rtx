#pragma once

#include <tuple>

#include <osg/Vec3f>

#include "areatype.hpp"

namespace DetourNavigator
{
    struct OffMeshConnection
    {
        osg::Vec3f mStart;
        osg::Vec3f mEnd;
        AreaType mAreaType;
    };

    inline bool operator<(const OffMeshConnection& lhs, const OffMeshConnection& rhs)
    {
        return std::tie(lhs.mStart, lhs.mEnd, lhs.mAreaType) < std::tie(rhs.mStart, rhs.mEnd, rhs.mAreaType);
    }
}
