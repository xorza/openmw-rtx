#pragma once

#include <sol/forward.hpp>

namespace ESM
{
    struct Sound;
}

namespace MWLua
{
    struct Context;

    sol::table initCoreSoundBindings(const Context& context);

    sol::table initAmbientPackage(const Context& context);

    void addMutableSoundType(sol::state_view& lua);

    ESM::Sound tableToSound(const sol::table&);
}
