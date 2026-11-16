#pragma once

#include <sol/table.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace LuaUi
{
    void registerAllWidgets();

    const std::unordered_map<std::string, std::string>& widgetTypeToName();

    void clearGameInterface();
    void clearMenuInterface();
    void updateAllElementCoords();

    bool warnUnused(std::vector<std::string>& warnings, const sol::object& object, const std::string& tableName,
        const std::vector<std::string_view>& usedKeys, bool generateWarningStrings);
}
