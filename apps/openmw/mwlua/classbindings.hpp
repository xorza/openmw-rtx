#pragma once

#include <sol/forward.hpp>

namespace ESM
{
    struct Class;
}

namespace MWLua
{
    struct Context;

    sol::table initClassRecordBindings(const Context& context);
    void addMutableClassType(sol::state_view& lua);
    ESM::Class tableToClass(const sol::table& rec);
}
