#pragma once

#include <sol/forward.hpp>

namespace ESM
{
    struct Faction;
}

namespace MWLua
{
    struct Context;

    sol::table initCoreFactionBindings(const Context& context);
    void addMutableFactionType(sol::state_view& lua);
    ESM::Faction tableToFaction(const sol::table& rec);
}
