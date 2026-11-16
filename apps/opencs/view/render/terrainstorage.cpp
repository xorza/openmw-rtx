#include "terrainstorage.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

#include <osg/Vec4ub>
#include <stdlib.h>

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
        const int index = mData.getLand().searchId(
            ESM::RefId::stringRefId(CSMWorld::Land::createUniqueRecordId(cellLocation.mX, cellLocation.mY)));
        if (index == -1)
            return nullptr;

        const ESM::Land& land = mData.getLand().getRecord(index).get();
        return std::make_shared<const ESMTerrain::LandObject>(
            land, ESM::Land::DATA_VHGT | ESM::Land::DATA_VNML | ESM::Land::DATA_VCLR | ESM::Land::DATA_VTEX);
    }

    const VFS::Path::Normalized* TerrainStorage::getLandTexture(std::uint16_t index, int plugin)
    {
        return mData.getLandTextures().getLandTexture(index, plugin);
    }

    void TerrainStorage::setAlteredHeight(int inCellX, int inCellY, float height)
    {
        mAlteredHeight[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(inCellX)]
            = static_cast<float>(height - fmod(height, 8)); // Limit to divisible by 8 to avoid cell seam breakage
    }

    void TerrainStorage::resetHeights()
    {
        std::fill(std::begin(mAlteredHeight), std::end(mAlteredHeight), 0);
    }

    float TerrainStorage::getSumOfAlteredAndTrueHeight(int cellX, int cellY, int inCellX, int inCellY)
    {
        float height = 0.f;

        const int index
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

    float* TerrainStorage::getAlteredHeight(int inCellX, int inCellY)
    {
        return &mAlteredHeight[static_cast<std::size_t>(inCellY) * ESM::Land::LAND_SIZE
            + static_cast<std::size_t>(inCellX)];
    }

    void TerrainStorage::getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId worldspace)
    {
        // not needed at the moment - this returns the bounds of the whole world, but we only edit individual cells
        throw std::runtime_error("getBounds not implemented");
    }

    int TerrainStorage::getThisHeight(int col, int row, std::span<const float> heightData) const
    {
        return static_cast<int>(
            heightData[static_cast<std::size_t>(col) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<unsigned int>(col * ESM::Land::LAND_SIZE + row)]);
    }

    int TerrainStorage::getLeftHeight(int col, int row, std::span<const float> heightData) const
    {
        return static_cast<int>(heightData[static_cast<std::size_t>((col)*ESM::Land::LAND_SIZE + row - 1)]
            + mAlteredHeight[static_cast<unsigned int>((col)*ESM::Land::LAND_SIZE + row - 1)]);
    }

    int TerrainStorage::getRightHeight(int col, int row, std::span<const float> heightData) const
    {
        return static_cast<int>(
            heightData[static_cast<std::size_t>(col) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row) + 1]
            + mAlteredHeight[static_cast<unsigned int>(col * ESM::Land::LAND_SIZE + row + 1)]);
    }

    int TerrainStorage::getUpHeight(int col, int row, std::span<const float> heightData) const
    {
        return static_cast<int>(
            heightData[(static_cast<std::size_t>(col) - 1) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<unsigned int>((col - 1) * ESM::Land::LAND_SIZE + row)]);
    }

    int TerrainStorage::getDownHeight(int col, int row, std::span<const float> heightData) const
    {
        return static_cast<int>(
            heightData[(static_cast<std::size_t>(col) + 1) * ESM::Land::LAND_SIZE + static_cast<std::size_t>(row)]
            + mAlteredHeight[static_cast<unsigned int>((col + 1) * ESM::Land::LAND_SIZE + row)]);
    }

    int TerrainStorage::getHeightDifferenceToLeft(int col, int row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getLeftHeight(col, row, heightData));
    }

    int TerrainStorage::getHeightDifferenceToRight(int col, int row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getRightHeight(col, row, heightData));
    }

    int TerrainStorage::getHeightDifferenceToUp(int col, int row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getUpHeight(col, row, heightData));
    }

    int TerrainStorage::getHeightDifferenceToDown(int col, int row, std::span<const float> heightData) const
    {
        return abs(getThisHeight(col, row, heightData) - getDownHeight(col, row, heightData));
    }

    bool TerrainStorage::leftOrUpIsOverTheLimit(
        int col, int row, int heightWarningLimit, std::span<const float> heightData) const
    {
        return getHeightDifferenceToLeft(col, row, heightData) >= heightWarningLimit
            || getHeightDifferenceToUp(col, row, heightData) >= heightWarningLimit;
    }

    bool TerrainStorage::rightOrDownIsOverTheLimit(
        int col, int row, int heightWarningLimit, std::span<const float> heightData) const
    {
        return getHeightDifferenceToRight(col, row, heightData) >= heightWarningLimit
            || getHeightDifferenceToDown(col, row, heightData) >= heightWarningLimit;
    }

    void TerrainStorage::adjustColor(int col, int row, const ESM::LandData* heightData, osg::Vec4ub& color) const
    {
        if (!heightData)
            return;
        // Highlight broken height changes
        int heightWarningLimit = 1024;
        if (((col > 0 && row > 0) && leftOrUpIsOverTheLimit(col, row, heightWarningLimit, heightData->getHeights()))
            || ((col < ESM::Land::LAND_SIZE - 1 && row < ESM::Land::LAND_SIZE - 1)
                && rightOrDownIsOverTheLimit(col, row, heightWarningLimit, heightData->getHeights())))
        {
            color.r() = 255;
            color.g() = 0;
            color.b() = 0;
        }
    }

    float TerrainStorage::getAlteredHeight(int col, int row) const
    {
        return mAlteredHeight[static_cast<unsigned int>(col * ESM::Land::LAND_SIZE + row)];
    }
}
