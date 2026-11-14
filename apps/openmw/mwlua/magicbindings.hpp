#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initCoreMagicBindings(const Context& context);
    void addActorMagicBindings(sol::table& actor, const Context& context);
}
