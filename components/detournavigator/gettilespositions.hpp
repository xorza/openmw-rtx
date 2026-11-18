#pragma once

#include <cstdint>

#include "tilebounds.hpp"
#include "tileposition.hpp"
#include "tilespositionsrange.hpp"

class btVector3;
class btTransform;
class btCollisionShape;

namespace osg
{
    class Vec2f;
}

namespace DetourNavigator
{
    struct RecastSettings;

    TilesPositionsRange makeTilesPositionsRange(
        const osg::Vec2f& aabbMin, const osg::Vec2f& aabbMax, const RecastSettings& settings);

    TilesPositionsRange makeTilesPositionsRange(
        const btCollisionShape& shape, const btTransform& transform, const RecastSettings& settings);

    TilesPositionsRange makeTilesPositionsRange(const btCollisionShape& shape, const btTransform& transform,
        const TileBounds& bounds, const RecastSettings& settings);

    TilesPositionsRange makeTilesPositionsRange(
        const std::int32_t cellSize, const btVector3& shift, const RecastSettings& settings);

    template <class Callback>
    inline void getTilesPositions(const TilesPositionsRange& range, Callback&& callback)
    {
        for (std::int32_t tileX = range.mBegin.x(); tileX < range.mEnd.x(); ++tileX)
            for (std::int32_t tileY = range.mBegin.y(); tileY < range.mEnd.y(); ++tileY)
                callback(TilePosition{ tileX, tileY });
    }

    inline bool isInTilesPositionsRange(std::int32_t begin, std::int32_t end, std::int32_t coordinate)
    {
        return begin <= coordinate && coordinate < end;
    }

    inline bool isInTilesPositionsRange(const TilesPositionsRange& range, const TilePosition& position)
    {
        return isInTilesPositionsRange(range.mBegin.x(), range.mEnd.x(), position.x())
            && isInTilesPositionsRange(range.mBegin.y(), range.mEnd.y(), position.y());
    }

    TilesPositionsRange getIntersection(const TilesPositionsRange& a, const TilesPositionsRange& b) noexcept;

    TilesPositionsRange getUnion(const TilesPositionsRange& a, const TilesPositionsRange& b) noexcept;
}
