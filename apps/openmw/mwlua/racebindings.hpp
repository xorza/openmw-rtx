#pragma once

#include <sol/forward.hpp>

namespace ESM
{
    struct Race;
}

namespace MWLua
{
    struct Context;

    sol::table initRaceRecordBindings(const Context& context);
    void addMutableRaceType(sol::state_view& lua);
    ESM::Race tableToRace(const sol::table& rec);
}
