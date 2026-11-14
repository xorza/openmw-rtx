#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initInputPackage(const Context& context);
}
