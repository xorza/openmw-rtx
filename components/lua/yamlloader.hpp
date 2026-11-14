#pragma once

#include <iosfwd>
#include <string>

#include <sol/forward.hpp>

namespace LuaUtil
{
    sol::object loadYaml(const std::string& input, const sol::state_view& lua);

    sol::object loadYaml(std::istream& input, const sol::state_view& lua);
}
