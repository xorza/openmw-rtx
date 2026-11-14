#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initCoreMwScriptBindings(const Context& context);
}
