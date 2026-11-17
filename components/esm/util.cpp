#include "util.hpp"

#include <cstdint>

#include <components/esm3/loadland.hpp>
#include <components/esm4/loadland.hpp>

osg::Vec2f ESM::indexToPosition(const ESM::ExteriorCellLocation& cellIndex, bool centre)
{
    const std::int32_t cellSize = ESM::getCellSize(cellIndex.mWorldspace);

    auto x = static_cast<float>(cellSize * cellIndex.mX);
    auto y = static_cast<float>(cellSize * cellIndex.mY);

    if (centre)
    {
        // A whole-number division on purpose.
        // NOLINTNEXTLINE(bugprone-integer-division)
        x += static_cast<float>(cellSize / 2);
        // A whole-number division on purpose.
        // NOLINTNEXTLINE(bugprone-integer-division)
        y += static_cast<float>(cellSize / 2);
    }

    return osg::Vec2f(x, y);
}

std::int32_t ESM::getLandSize(ESM::RefId worldspaceId)
{
    return isEsm4Ext(worldspaceId) ? ESM4::Land::sVertsPerSide : ESM::Land::LAND_SIZE;
}
