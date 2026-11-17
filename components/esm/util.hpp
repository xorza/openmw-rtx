#pragma once

#include <cmath>
#include <cstdint>

#include <osg/Vec2f>

#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>

#include "exteriorcelllocation.hpp"
#include "refid.hpp"

namespace ESM
{
    inline bool isEsm4Ext(ESM::RefId worldspaceId)
    {
        return worldspaceId != ESM::Cell::sDefaultWorldspaceId;
    }

    inline std::int32_t getCellSize(ESM::RefId worldspaceId)
    {
        return isEsm4Ext(worldspaceId) ? Constants::ESM4CellSizeInUnits : Constants::CellSizeInUnits;
    }

    // Vertex count of a side of a land record
    std::int32_t getLandSize(ESM::RefId worldspaceId);

    inline ESM::ExteriorCellLocation positionToExteriorCellLocation(
        float x, float y, ESM::RefId worldspaceId = ESM::Cell::sDefaultWorldspaceId)
    {
        const auto cellSize = static_cast<float>(getCellSize(worldspaceId));
        return { static_cast<std::int32_t>(std::floor(x / cellSize)),
            static_cast<std::int32_t>(std::floor(y / cellSize)), worldspaceId };
    }

    // Convert exterior cell location to position.
    osg::Vec2f indexToPosition(const ESM::ExteriorCellLocation& cellIndex, bool centre = false);
}
