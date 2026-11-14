#pragma once

#include <sol/state.hpp>

#include <memory>

namespace LuaUtil
{
    struct CloseLuaState
    {
        void operator()(lua_State* state) noexcept { lua_close(state); }
    };

    using LuaStatePtr = std::unique_ptr<lua_State, CloseLuaState>;
}
