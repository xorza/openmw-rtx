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
    struct GroundcoverCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<bool> mEnabled{ mIndex, "Groundcover", "enabled" };
        SettingValue<float> mDensity{ mIndex, "Groundcover", "density", makeClampSanitizerFloat(0, 1) };
        SettingValue<float> mRenderingDistance{ mIndex, "Groundcover", "rendering distance", makeMaxSanitizerFloat(0) };
        SettingValue<std::int32_t> mStompMode{ mIndex, "Groundcover", "stomp mode", makeEnumSanitizerInt({ 0, 1, 2 }) };
        SettingValue<std::int32_t> mStompIntensity{ mIndex, "Groundcover", "stomp intensity",
            makeEnumSanitizerInt({ 0, 1, 2 }) };
        SettingValue<bool> mPointLighting{ mIndex, "Groundcover", "point lighting" };
    };
}
