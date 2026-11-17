#pragma once

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <random>
#include <type_traits>

namespace DetourNavigator
{
    namespace Tests
    {
        template <class T, class Random>
        inline auto generateValue(T& value, Random& random) -> std::enable_if_t<sizeof(T) >= 2>
        {
            using Distribution = std::conditional_t<std::is_floating_point_v<T>, std::uniform_real_distribution<T>,
                std::uniform_int_distribution<T>>;
            Distribution distribution(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
            value = distribution(random);
        }

        template <class T, class Random>
        inline auto generateValue(T& value, Random& random) -> std::enable_if_t<sizeof(T) == 1>
        {
            std::uint16_t v = 0;
            generateValue(v, random);
            value = static_cast<T>(v % 256);
        }

        template <class Random>
        inline void generateValue(std::uint8_t& value, Random& random)
        {
            std::uint16_t v = 0;
            generateValue(v, random);
            value = static_cast<std::uint8_t>(v % 256);
        }

        template <class I, class Random>
        inline void generateRange(I begin, I end, Random& random)
        {
            std::for_each(begin, end, [&](auto& v) { generateValue(v, random); });
        }
    }
}
