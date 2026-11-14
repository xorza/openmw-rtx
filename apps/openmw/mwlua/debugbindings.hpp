#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initDebugPackage(const Context& context);
}
