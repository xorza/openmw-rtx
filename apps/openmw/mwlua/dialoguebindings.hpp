#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initCoreDialogueBindings(const Context& context);
}
