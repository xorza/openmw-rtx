#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    void addItemDataBindings(sol::table& item, const Context& context);

}
