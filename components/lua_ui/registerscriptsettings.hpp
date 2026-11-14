#pragma once

#include <sol/sol.hpp>

namespace LuaUi
{
    // implemented in scriptsettings.cpp
    void registerSettingsPage(const sol::table& options);
    void clearSettings();
    void removeSettingsPage(const sol::table& options);
}
