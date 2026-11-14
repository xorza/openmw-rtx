#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    void addCoreTimeBindings(sol::table& api, const Context& context);

    sol::table initCorePackage(const Context& context);
}
