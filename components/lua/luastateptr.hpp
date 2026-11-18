#pragma once

#include <memory>

#include <sol/state.hpp>

namespace LuaUtil
{
    struct CloseLuaState
    {
        void operator()(lua_State* state) noexcept { lua_close(state); }
    };

    using LuaStatePtr = std::unique_ptr<lua_State, CloseLuaState>;
}
