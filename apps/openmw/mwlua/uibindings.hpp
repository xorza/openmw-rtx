#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initUserInterfacePackage(const Context& context);
}
