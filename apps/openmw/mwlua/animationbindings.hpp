#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initAnimationPackage(const Context& context);
    sol::table initWorldVfxBindings(const Context& context);
}
