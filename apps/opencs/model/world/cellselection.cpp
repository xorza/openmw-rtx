#include "cellselection.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

#include "cellcoordinates.hpp"

CSMWorld::CellSelection::Iterator CSMWorld::CellSelection::begin() const
{
    return mCells.begin();
}

CSMWorld::CellSelection::Iterator CSMWorld::CellSelection::end() const
{
    return mCells.end();
}

bool CSMWorld::CellSelection::add(const CellCoordinates& coordinates)
{
    return mCells.insert(coordinates).second;
}

void CSMWorld::CellSelection::remove(const CellCoordinates& coordinates)
{
    mCells.erase(coordinates);
}

bool CSMWorld::CellSelection::has(const CellCoordinates& coordinates) const
{
    return mCells.find(coordinates) != end();
}

std::int32_t CSMWorld::CellSelection::getSize() const
{
    return static_cast<std::int32_t>(mCells.size());
}

CSMWorld::CellCoordinates CSMWorld::CellSelection::getCentre() const
{
    if (mCells.empty())
        throw std::logic_error("call of getCentre on empty cell selection");

    double x = 0;
    double y = 0;

    for (auto entry : *this)
    {
        x += entry.getX();
        y += entry.getY();
    }

    x /= static_cast<double>(mCells.size());
    y /= static_cast<double>(mCells.size());

    auto closest = begin();
    double distance = std::numeric_limits<double>::max();

    for (auto iter(begin()); iter != end(); ++iter)
    {
        double deltaX = x - iter->getX();
        double deltaY = y - iter->getY();

        double delta = std::sqrt(deltaX * deltaX + deltaY * deltaY);

        if (delta < distance)
        {
            distance = delta;
            closest = iter;
        }
    }

    return *closest;
}

void CSMWorld::CellSelection::move(std::int32_t x, std::int32_t y)
{
    Container moved;

    for (auto entry : *this)
        moved.insert(entry.move(x, y));

    mCells.swap(moved);
}
