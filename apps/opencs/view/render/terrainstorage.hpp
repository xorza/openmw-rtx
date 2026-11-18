#pragma once

#include <array>
#include <cstdint>

#include <osg/ref_ptr>

#include <components/esm3/loadland.hpp>
#include <components/esm3/loadltex.hpp>
#include <components/esmterrain/storage.hpp>

namespace CSMWorld
{
    class Data;
}
namespace osg
{
    class Vec4ub;
}

namespace CSVRender
{
    /**
     * @brief A bridge between the terrain component and OpenCS's terrain data storage.
     */
    class TerrainStorage : public ESMTerrain::Storage
    {
    public:
        explicit TerrainStorage(const CSMWorld::Data& data);
        void setAlteredHeight(std::int32_t inCellX, std::int32_t inCellY, float heightMap);
        void resetHeights();

        bool useAlteration() const override { return true; }
        float getSumOfAlteredAndTrueHeight(
            std::int32_t cellX, std::int32_t cellY, std::int32_t inCellX, std::int32_t inCellY);
        float* getAlteredHeight(std::int32_t inCellX, std::int32_t inCellY);

    private:
        const CSMWorld::Data& mData;
        std::array<float, ESM::Land::LAND_SIZE * ESM::Land::LAND_SIZE> mAlteredHeight{};

        std::shared_ptr<const ESMTerrain::LandObject> getLand(ESM::ExteriorCellLocation cellLocation) override;
        const VFS::Path::Normalized* getLandTexture(std::uint16_t index, std::int32_t plugin) override;

        void getBounds(float& minX, float& maxX, float& minY, float& maxY, ESM::RefId worldspace) override;

        std::int32_t getThisHeight(std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getLeftHeight(std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getRightHeight(std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getUpHeight(std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getDownHeight(std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getHeightDifferenceToLeft(
            std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getHeightDifferenceToRight(
            std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getHeightDifferenceToUp(
            std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        std::int32_t getHeightDifferenceToDown(
            std::int32_t col, std::int32_t row, std::span<const float> heightData) const;
        bool leftOrUpIsOverTheLimit(std::int32_t col, std::int32_t row, std::int32_t heightWarningLimit,
            std::span<const float> heightData) const;
        bool rightOrDownIsOverTheLimit(std::int32_t col, std::int32_t row, std::int32_t heightWarningLimit,
            std::span<const float> heightData) const;

        void adjustColor(
            std::int32_t col, std::int32_t row, const ESM::LandData* heightData, osg::Vec4ub& color) const override;
        float getAlteredHeight(std::int32_t col, std::int32_t row) const override;
    };

}
