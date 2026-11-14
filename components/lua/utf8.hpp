#pragma once

#include <sol/sol.hpp>

namespace LuaUtf8
{
    sol::table initUtf8Package(sol::state_view&);
}
