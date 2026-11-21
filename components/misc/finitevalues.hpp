#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <components/lua/luastate.hpp>

namespace Misc
{
    template <class T>
    struct FiniteValue
    {
        T mValue;

        // A finite value converts from and to the value it checks.
        // NOLINTNEXTLINE(google-explicit-constructor)
        FiniteValue(T v)
        {
            constexpr bool isVecType
                = std::is_same_v<glm::vec2, T> || std::is_same_v<glm::vec3, T> || std::is_same_v<glm::vec4, T>;

            if constexpr (isVecType)
            {
                for (glm::length_t i = 0; i < T::length(); ++i)
                    if (!std::isfinite(v[i]))
                        throw std::invalid_argument("Vector must contain finite numbers");
            }
            else
            {
                if (!std::isfinite(v))
                    throw std::invalid_argument("Value must be a finite number");
            }
            mValue = v;
        }

        // NOLINTNEXTLINE(google-explicit-constructor)
        operator T() const { return mValue; }
    };

    using FiniteDouble = FiniteValue<double>;

    using FiniteFloat = FiniteValue<float>;

    using FiniteVec2f = FiniteValue<glm::vec2>;

    using FiniteVec3f = FiniteValue<glm::vec3>;

    using FiniteVec4f = FiniteValue<glm::vec4>;
}

namespace sol
{
    template <class Handler, class T>
    bool sol_lua_check(
        types<Misc::FiniteValue<T>>, lua_State* state, std::int32_t index, Handler&& handler, stack::record& tracking)
    {
        bool success = stack::check<T>(state, lua_absindex(state, index), std::forward<Handler>(handler));
        tracking.use(1);
        return success;
    }

    template <class T>
    static Misc::FiniteValue<T> sol_lua_get(
        types<Misc::FiniteValue<T>>, lua_State* state, std::int32_t index, stack::record& tracking)
    {
        T value = stack::get<T>(state, lua_absindex(state, index));
        tracking.use(1);
        return value;
    }
}
