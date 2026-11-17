#include "cellcoordinates.hpp"

#include <cmath>
#include <cstdint>
#include <sstream>

#include <osg/Vec3d>

#include <components/esm/refid.hpp>
#include <components/esm3/loadland.hpp>
#include <components/misc/constants.hpp>

CSMWorld::CellCoordinates::CellCoordinates()
    : mX(0)
    , mY(0)
{
}

CSMWorld::CellCoordinates::CellCoordinates(std::int32_t x, std::int32_t y)
    : mX(x)
    , mY(y)
{
}

CSMWorld::CellCoordinates::CellCoordinates(const std::pair<std::int32_t, std::int32_t>& coordinates)
    : mX(coordinates.first)
    , mY(coordinates.second)
{
}

std::int32_t CSMWorld::CellCoordinates::getX() const
{
    return mX;
}

std::int32_t CSMWorld::CellCoordinates::getY() const
{
    return mY;
}

CSMWorld::CellCoordinates CSMWorld::CellCoordinates::move(std::int32_t x, std::int32_t y) const
{
    return CellCoordinates(mX + x, mY + y);
}

std::string CSMWorld::CellCoordinates::getId(const std::string& worldspace) const
{
    // we ignore the worldspace for now, since there is only one (will change in 1.1)
    return generateId(mX, mY);
}

std::string CSMWorld::CellCoordinates::generateId(std::int32_t x, std::int32_t y)
{
    std::string cellId = "#" + std::to_string(x) + " " + std::to_string(y);
    return cellId;
}

bool CSMWorld::CellCoordinates::isExteriorCell(const std::string& id)
{
    return (!id.empty() && id[0] == '#');
}

bool CSMWorld::CellCoordinates::isExteriorCell(const ESM::RefId& id)
{
    return id.startsWith("#");
}

std::pair<CSMWorld::CellCoordinates, bool> CSMWorld::CellCoordinates::fromId(const std::string& id)
{
    // no worldspace for now, needs to be changed for 1.1
    if (isExteriorCell(id))
    {
        std::int32_t x = 0, y = 0;
        char ignore = 0;

        std::istringstream stream(id);
        if (stream >> ignore >> x >> y)
            return std::make_pair(CellCoordinates(x, y), true);
    }

    return std::make_pair(CellCoordinates(), false);
}

std::pair<std::int32_t, std::int32_t> CSMWorld::CellCoordinates::coordinatesToCellIndex(float x, float y)
{
    return std::make_pair(std::floor(x / Constants::CellSizeInUnits), std::floor(y / Constants::CellSizeInUnits));
}

std::pair<std::int32_t, std::int32_t> CSMWorld::CellCoordinates::toTextureCoords(const osg::Vec3d& worldPos)
{
    const auto xd = static_cast<float>(worldPos.x() * ESM::Land::LAND_TEXTURE_SIZE / ESM::Land::REAL_SIZE - 0.25f);
    const auto yd = static_cast<float>(worldPos.y() * ESM::Land::LAND_TEXTURE_SIZE / ESM::Land::REAL_SIZE + 0.25f);

    const auto x = static_cast<std::int32_t>(std::floor(xd));
    const auto y = static_cast<std::int32_t>(std::floor(yd));

    return std::make_pair(x, y);
}

std::pair<std::int32_t, std::int32_t> CSMWorld::CellCoordinates::toVertexCoords(const osg::Vec3d& worldPos)
{
    const auto xd = static_cast<float>(worldPos.x() * (ESM::Land::LAND_SIZE - 1) / ESM::Land::REAL_SIZE + 0.5f);
    const auto yd = static_cast<float>(worldPos.y() * (ESM::Land::LAND_SIZE - 1) / ESM::Land::REAL_SIZE + 0.5f);

    const auto x = static_cast<std::int32_t>(std::floor(xd));
    const auto y = static_cast<std::int32_t>(std::floor(yd));

    return std::make_pair(x, y);
}

float CSMWorld::CellCoordinates::textureGlobalXToWorldCoords(std::int32_t textureGlobal)
{
    return ESM::Land::REAL_SIZE * (static_cast<float>(textureGlobal) + 0.25f) / ESM::Land::LAND_TEXTURE_SIZE;
}

float CSMWorld::CellCoordinates::textureGlobalYToWorldCoords(std::int32_t textureGlobal)
{
    return ESM::Land::REAL_SIZE * (static_cast<float>(textureGlobal) - 0.25f) / ESM::Land::LAND_TEXTURE_SIZE;
}

float CSMWorld::CellCoordinates::vertexGlobalToWorldCoords(std::int32_t vertexGlobal)
{
    return ESM::Land::REAL_SIZE * static_cast<float>(vertexGlobal) / (ESM::Land::LAND_SIZE - 1);
}

std::int32_t CSMWorld::CellCoordinates::vertexGlobalToInCellCoords(std::int32_t vertexGlobal)
{
    return static_cast<std::int32_t>(static_cast<float>(vertexGlobal)
        - std::floor(static_cast<float>(vertexGlobal) / (ESM::Land::LAND_SIZE - 1)) * (ESM::Land::LAND_SIZE - 1));
}

std::string CSMWorld::CellCoordinates::textureGlobalToCellId(const std::pair<std::int32_t, std::int32_t>& textureGlobal)
{
    auto x
        = static_cast<std::int32_t>(std::floor(static_cast<float>(textureGlobal.first) / ESM::Land::LAND_TEXTURE_SIZE));
    auto y = static_cast<std::int32_t>(
        std::floor(static_cast<float>(textureGlobal.second) / ESM::Land::LAND_TEXTURE_SIZE));
    return generateId(x, y);
}

std::string CSMWorld::CellCoordinates::vertexGlobalToCellId(const std::pair<std::int32_t, std::int32_t>& vertexGlobal)
{
    auto x = static_cast<std::int32_t>(std::floor(static_cast<float>(vertexGlobal.first) / (ESM::Land::LAND_SIZE - 1)));
    auto y
        = static_cast<std::int32_t>(std::floor(static_cast<float>(vertexGlobal.second) / (ESM::Land::LAND_SIZE - 1)));
    return generateId(x, y);
}

bool CSMWorld::operator==(const CellCoordinates& left, const CellCoordinates& right)
{
    return left.getX() == right.getX() && left.getY() == right.getY();
}

bool CSMWorld::operator!=(const CellCoordinates& left, const CellCoordinates& right)
{
    return !(left == right);
}

bool CSMWorld::operator<(const CellCoordinates& left, const CellCoordinates& right)
{
    if (left.getX() < right.getX())
        return true;

    if (left.getX() > right.getX())
        return false;

    return left.getY() < right.getY();
}

std::ostream& CSMWorld::operator<<(std::ostream& stream, const CellCoordinates& coordiantes)
{
    return stream << coordiantes.getX() << ", " << coordiantes.getY();
}
