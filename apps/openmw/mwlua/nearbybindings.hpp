#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initNearbyPackage(const Context& context);
}
