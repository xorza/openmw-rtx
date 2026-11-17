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
    struct SavesCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<std::string> mCharacter{ mIndex, "Saves", "character" };
        SettingValue<bool> mAutosave{ mIndex, "Saves", "autosave" };
        SettingValue<std::int32_t> mMaxQuicksaves{ mIndex, "Saves", "max quicksaves", makeMaxSanitizerInt(1) };
    };
}
