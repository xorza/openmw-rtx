#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>

namespace Settings
{
    struct TerrainCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mDistantTerrain{ mIndex, "Terrain", "distant terrain" };
        SettingValue<float> mLodFactor{ mIndex, "Terrain", "lod factor", makeMaxStrictSanitizerFloat(0) };
        SettingValue<std::int32_t> mVertexLodMod{ mIndex, "Terrain", "vertex lod mod" };
        SettingValue<std::int32_t> mCompositeMapLevel{ mIndex, "Terrain", "composite map level",
            makeMaxSanitizerInt(-3) };
        SettingValue<std::int32_t> mCompositeMapResolution{ mIndex, "Terrain", "composite map resolution",
            makeMaxSanitizerInt(1) };
        SettingValue<float> mMaxCompositeGeometrySize{ mIndex, "Terrain", "max composite geometry size",
            makeMaxSanitizerFloat(1) };
        SettingValue<bool> mDebugChunks{ mIndex, "Terrain", "debug chunks" };
        SettingValue<bool> mObjectPaging{ mIndex, "Terrain", "object paging" };
        SettingValue<bool> mObjectPagingActiveGrid{ mIndex, "Terrain", "object paging active grid" };
        SettingValue<float> mObjectPagingMergeFactor{ mIndex, "Terrain", "object paging merge factor",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSize{ mIndex, "Terrain", "object paging min size",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSizeMergeFactor{ mIndex, "Terrain", "object paging min size merge factor",
            makeMaxStrictSanitizerFloat(0) };
        SettingValue<float> mObjectPagingMinSizeCostMultiplier{ mIndex, "Terrain",
            "object paging min size cost multiplier", makeMaxStrictSanitizerFloat(0) };
        SettingValue<bool> mWaterCulling{ mIndex, "Terrain", "water culling" };
    };
}
