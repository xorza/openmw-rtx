#pragma once

#include <sol/sol.hpp>

namespace L10n
{
    class Manager;
}

namespace LuaUtil
{
    sol::function initL10nLoader(lua_State* state, L10n::Manager* manager);
}
