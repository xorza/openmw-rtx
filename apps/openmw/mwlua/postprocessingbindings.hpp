#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initPostprocessingPackage(const Context& context);
}
