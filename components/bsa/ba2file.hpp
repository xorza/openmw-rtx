#pragma once

#include <cstdint>
#include <string_view>

namespace Bsa
{
    std::uint32_t generateHash(std::string_view name);
    std::uint32_t generateExtensionHash(std::string_view extension);

    enum class BA2Version : std::uint32_t
    {
        Fallout4 = 1,
        StarfieldGeneral = 2,
        StarfieldDDS = 3,
        Fallout4NextGen_v7 = 7,
        Fallout4NextGen_v8 = 8,
    };
}
