#include "terrainstorage.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <osg/Vec4ub>

#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/land.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <components/esmterrain/storage.hpp>
#include <components/resource/resourcesystem.hpp> // IWYU pragma: keep: the resource system whole, which a shared pointer to it is read through

namespace CSVRender
{
    TerrainStorage::TerrainStorage(const CSMWorld::Data& data)
        : ESMTerrain::Storage(data.getResourceSystem()->getVFS())
        , mData(data)
    {
        resetHeights();
    }

    std::shared_ptr<const ESMTerrain::LandObject> TerrainStorage::getLand(ESM::ExteriorCellLocation cellLocation)
    {
        // The cell isn't guaranteed to have Land. This is because the terrain implementation
        // has to wrap the vertices of the last row and column to the next cell, which may be a nonexisting cell
        const std::int32_t index = mData.getLand().searchId(
            ESM::RefId::stringRefId(CSMWorld::Land::createUniqueRecordId(cellLocation.mX, cellLocation.mY)));
        if (index == -1)
            return nullptr;

        const ESM::Land& land = mData.getLand().getRecord(index).get();
        return std::make_shared<const ESMTerrain::LandObject>(
            land, ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VCLR | ESM::Land::DATA_VTEX);
    }

    const VFS::Path::Normalized* TerrainStorage::getLandTexture(std::uint16_t index, std::int32_t plugin)
    {
        return mData.getLandTextures().getLandTexture(index, plugin);
    }

    void TerrainStorage::setAlteredHeight(std::int32_t inCellX, std::int32_t inCellY, float height)
    {
        mAlteredHeight[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(inCellX)]
            = static_cast<float>(height - fmod(height, 8)); // Limit to divisible by 8 to avoid cell seam breakage
    }

    void TerrainStorage::resetHeights()
    {
        std::fill(std::begin(mAlteredHeight), std::end(mAlteredHeight), 0);
    }

    float TerrainStorage::getSumOfAlteredAndTrueHeight(
        std::int32_t cellX, std::int32_t cellY, std::int32_t inCellX, std::int32_t inCellY)
    {
        float height = 0.f;

        const std::int32_t index
            = mData.getLand().searchId(ESM::RefId::stringRefId(CSMWorld::Land::createUniqueRecordId(cellX, cellY)));
        if (index == -1) // no land!
            return height;

        const ESM::Land::LandData* landData = mData.getLand().getRecord(index).get().getLandData(ESM::Land::DATA_VHGT);
        height = landData->mHeights[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE
            + static_cast<std::size_t>(inCellX)];
        return mAlteredHeight[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE
                   + static_cast<std::size_t>(inCellX)]
            + height;
    }

    float* TerrainStorage::getAlteredHeight(std::int32_t inCellX, std::int32_t inCellY)
    {
        return &mAlteredHeight[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE
            + static_cast<std::size_t>(inCellX)];
    }

    void TerrainStorage::getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId worldspace)
    {
        // not needed at the moment - this returns the bounds of the whole world, but we only edit individual cells
        throw std::runtime_error("getBounds not implemented");
    }

    std::int32_t TerrainStorage::getThisHeight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return static_cast<std::int32_t>(
            heightData[static_cast<std::size_t>(col) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<std::uint32_t>(col * ESM::Land::LAND_SIZE + row)]);
    }

    std::int32_t TerrainStorage::getLeftHeight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return static_cast<std::int32_t>(heightData[static_cast<std::size_t>((col)*ESM::Land::LAND_SIZE + row - 1)]
            + mAlteredHeight[static_cast<std::uint32_t>((col)*ESM::Land::LAND_SIZE + row - 1)]);
    }

    std::int32_t TerrainStorage::getRightHeight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return static_cast<std::int32_t>(
            heightData[static_cast<std::size_t>(col) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row) + 1]
            + mAlteredHeight[static_cast<std::uint32_t>(col * ESM::Land::LAND_SIZE + row + 1)]);
    }

    std::int32_t TerrainStorage::getUpHeight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return static_cast<std::int32_t>(
            heightData[(static_cast<std::size_t>(col) - 1) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<std::uint32_t>((col - 1) * ESM::Land::LAND_SIZE + row)]);
    }

    std::int32_t TerrainStorage::getDownHeight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return static_cast<std::int32_t>(
            heightData[(static_cast<std::size_t>(col) + 1) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<std::uint32_t>((col + 1) * ESM::Land::LAND_SIZE + row)]);
    }

    std::int32_t TerrainStorage::getHeightDifferenceToLeft(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getLeftHeight(col, row, heightData));
    }

    std::int32_t TerrainStorage::getHeightDifferenceToRight(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getRightHeight(col, row, heightData));
    }

    std::int32_t TerrainStorage::getHeightDifferenceToUp(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getUpHeight(col, row, heightData));
    }

    std::int32_t TerrainStorage::getHeightDifferenceToDown(
        std::int32_t col, std::int32_t row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getDownHeight(col, row, heightData));
    }

    bool TerrainStorage::leftOrUpIsOverTheLimit(
        std::int32_t col, std::int32_t row, std::int32_t heightWarningLimit, std::span<const float> heightData) const
    {
        return getHeightDifferenceToLeft(col, row, heightData) >= heightWarningLimit
            || getHeightDifferenceToUp(col, row, heightData) >= heightWarningLimit;
    }

    bool TerrainStorage::rightOrDownIsOverTheLimit(
        std::int32_t col, std::int32_t row, std::int32_t heightWarningLimit, std::span<const float> heightData) const
    {
        return getHeightDifferenceToRight(col, row, heightData) >= heightWarningLimit
            || getHeightDifferenceToDown(col, row, heightData) >= heightWarningLimit;
    }

    void TerrainStorage::adjustColor(
        std::int32_t col, std::int32_t row, const ESM::LandData* heightData, osg::Vec4ub& color) const
    {
        if (!heightData)
            return;
        // Highlight broken height changes
        std::int32_t heightWarningLimit = 1024;
        if (((col > 0 && row > 0) && leftOrUpIsOverTheLimit(col, row, heightWarningLimit, heightData->getHeights()))
            || ((col < ESM::Land::LAND_SIZE - 1 && row < ESM::Land::LAND_SIZE - 1)
                && rightOrDownIsOverTheLimit(col, row, heightWarningLimit, heightData->getHeights())))
        {
            color.r() = 255;
            color.g() = 0;
            color.b() = 0;
        }
    }

    float TerrainStorage::getAlteredHeight(std::int32_t col, std::int32_t row) const
    {
        return mAlteredHeight[static_cast<std::uint32_t>(col * ESM::Land::LAND_SIZE + row)];
    }
}
