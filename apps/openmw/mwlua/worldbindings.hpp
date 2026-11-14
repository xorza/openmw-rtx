#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initWorldPackage(const Context& context);
}
