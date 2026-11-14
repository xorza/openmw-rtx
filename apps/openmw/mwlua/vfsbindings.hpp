#pragma once

#include <sol/forward.hpp>

namespace MWLua
{
    struct Context;

    sol::table initVFSPackage(const Context& context);
}
