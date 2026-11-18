#include "ref.hpp"

#include <cstdint>

#include "cellcoordinates.hpp"

CSMWorld::CellRef::CellRef()
    : mNew(true)
    , mIdNum(0)
{
}

std::pair<std::int32_t, std::int32_t> CSMWorld::CellRef::getCellIndex() const
{
    return CellCoordinates::coordinatesToCellIndex(mPos.pos[0], mPos.pos[1]);
}
