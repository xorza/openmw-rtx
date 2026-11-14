#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initCoreLandBindings(const Context& context);
}
