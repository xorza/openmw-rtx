#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initMWScriptBindings(const Context& context);
}
