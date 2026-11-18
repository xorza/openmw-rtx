#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <components/esm4/loadland.hpp>

namespace ESM
{
    struct Land;
    struct LandRecordData;

    class LandData
    {
    public:
        LandData();
        explicit LandData(const ESM::Land& land, std::int32_t loadFlags);
        explicit LandData(const ESM4::Land& land, std::int32_t loadFlags);

        ~LandData();

        std::span<const float> getHeights() const { return mHeights; }
        std::span<const std::int8_t> getNormals() const { return mNormals; }
        std::span<const std::uint8_t> getColors() const { return mColors; }
        float getSize() const { return mSize; }
        float getMinHeight() const { return mMinHeight; }
        float getMaxHeight() const { return mMaxHeight; }
        std::int32_t getLandSize() const { return mLandSize; }
        std::int32_t getLoadFlags() const { return mLoadFlags; }
        std::int32_t getPlugin() const { return mPlugin; }

        bool isEsm4() const { return mIsEsm4; }

        std::span<const std::uint16_t> getTextures() const
        {
            if (mIsEsm4)
                throw std::logic_error("ESM3 textures requested from ESM4 LandData");
            return mTextures;
        }

        const ESM4::Land::Texture& getEsm4Texture(std::size_t quad) const
        {
            if (!mIsEsm4)
                throw std::logic_error("ESM4 texture requested from ESM3 LandData");
            return mEsm4Textures[quad];
        }

    private:
        std::unique_ptr<const ESM::LandRecordData> mData;
        std::vector<float> mHeightsData;
        std::span<const float> mHeights;
        std::span<const std::int8_t> mNormals;
        std::span<const std::uint8_t> mColors;
        std::span<const std::uint16_t> mTextures;
        std::array<ESM4::Land::Texture, 4> mEsm4Textures;
        std::int32_t mLoadFlags = 0;
        float mMinHeight = 0.f;
        float mMaxHeight = 0.f;
        float mSize = 0.f;
        std::int32_t mLandSize = 0;
        std::int32_t mPlugin = 0;
        bool mIsEsm4 = false;
    };

}
