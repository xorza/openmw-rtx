#pragma once

#include "tileposition.hpp"

#include <tuple>

namespace DetourNavigator
{
    struct TilesPositionsRange
    {
        TilePosition mBegin;
        TilePosition mEnd;
    };

    inline auto tie(const TilesPositionsRange& value)
    {
        return std::tie(value.mBegin, value.mEnd);
    }

    inline bool operator==(const TilesPositionsRange& lhs, const TilesPositionsRange& rhs)
    {
        return tie(lhs) == tie(rhs);
    }
}
