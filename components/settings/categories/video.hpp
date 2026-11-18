#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <osg/Math>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/sanitizerimpl.hpp>
#include <components/settings/settingvalue.hpp>
#include <components/settings/windowmode.hpp>

namespace Settings
{
    struct VideoCategory : WithIndex
    {
        using WithIndex::WithIndex;

        SettingValue<std::int32_t> mResolutionX{ mIndex, "Video", "resolution x", makeMaxSanitizerInt(0) };
        SettingValue<std::int32_t> mResolutionY{ mIndex, "Video", "resolution y", makeMaxSanitizerInt(0) };
        SettingValue<std::int32_t> mWindowWidth{ mIndex, "Video", "window width", makeMaxSanitizerInt(1) };
        SettingValue<std::int32_t> mWindowHeight{ mIndex, "Video", "window height", makeMaxSanitizerInt(1) };
        SettingValue<WindowMode> mWindowMode{ mIndex, "Video", "window mode" };
        SettingValue<std::int32_t> mScreen{ mIndex, "Video", "screen", makeMaxSanitizerInt(0) };
        SettingValue<bool> mMinimizeOnFocusLoss{ mIndex, "Video", "minimize on focus loss" };
        SettingValue<bool> mWindowBorder{ mIndex, "Video", "window border" };
        SettingValue<std::int32_t> mAntialiasing{ mIndex, "Video", "antialiasing", makeMaxSanitizerInt(0) };
        SettingValue<SDLUtil::VSyncMode> mVsyncMode{ mIndex, "Video", "vsync mode" };
        SettingValue<float> mFramerateLimit{ mIndex, "Video", "framerate limit", makeMaxSanitizerFloat(0) };
        SettingValue<float> mGamma{ mIndex, "Video", "gamma", makeMaxStrictSanitizerFloat(0) };
    };
}
