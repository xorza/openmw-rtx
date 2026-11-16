#pragma once

#include <tuple>

#include <osg/Vec3f>

#include "collisionshapetype.hpp"

namespace DetourNavigator
{
    struct AgentBounds
    {
        CollisionShapeType mShapeType;
        osg::Vec3f mHalfExtents;
    };

    inline auto tie(const AgentBounds& value)
    {
        return std::tie(value.mShapeType, value.mHalfExtents);
    }

    inline bool operator==(const AgentBounds& lhs, const AgentBounds& rhs)
    {
        return tie(lhs) == tie(rhs);
    }

    inline bool operator<(const AgentBounds& lhs, const AgentBounds& rhs)
    {
        return tie(lhs) < tie(rhs);
    }
}
