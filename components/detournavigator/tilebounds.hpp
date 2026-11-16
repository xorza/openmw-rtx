#pragma once

#include <algorithm>
#include <optional>
#include <tuple>

#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <LinearMath/btTransform.h>
#include <osg/Vec2f>
#include <osg/Vec2i>

#include <components/misc/convert.hpp>

namespace DetourNavigator
{
    struct TileBounds
    {
        osg::Vec2f mMin;
        osg::Vec2f mMax;
    };

    inline auto tie(const TileBounds& value) noexcept
    {
        return std::tie(value.mMin, value.mMax);
    }

    inline bool operator<(const TileBounds& lhs, const TileBounds& rhs) noexcept
    {
        return tie(lhs) < tie(rhs);
    }

    inline bool operator==(const TileBounds& lhs, const TileBounds& rhs) noexcept
    {
        return tie(lhs) == tie(rhs);
    }

    inline bool operator!=(const TileBounds& lhs, const TileBounds& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    inline std::optional<TileBounds> getIntersection(const TileBounds& a, const TileBounds& b) noexcept
    {
        const float minX = std::max(a.mMin.x(), b.mMin.x());
        const float maxX = std::min(a.mMax.x(), b.mMax.x());
        if (minX > maxX)
            return std::nullopt;
        const float minY = std::max(a.mMin.y(), b.mMin.y());
        const float maxY = std::min(a.mMax.y(), b.mMax.y());
        if (minY > maxY)
            return std::nullopt;
        return TileBounds{ osg::Vec2f(minX, minY), osg::Vec2f(maxX, maxY) };
    }

    inline TileBounds maxCellTileBounds(const osg::Vec2i& position, int size)
    {
        return TileBounds{ osg::Vec2f(static_cast<float>(position.x()), static_cast<float>(position.y()))
                * static_cast<float>(size),
            osg::Vec2f(static_cast<float>(position.x() + 1), static_cast<float>(position.y() + 1))
                * static_cast<float>(size) };
    }

    inline TileBounds makeObjectTileBounds(const btCollisionShape& shape, const btTransform& transform)
    {
        btVector3 aabbMin;
        btVector3 aabbMax;
        shape.getAabb(transform, aabbMin, aabbMax);
        return TileBounds{ Misc::Convert::toOsgXY(aabbMin), Misc::Convert::toOsgXY(aabbMax) };
    }
}
