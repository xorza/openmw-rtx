#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initMarkupPackage(const Context& context);
}
