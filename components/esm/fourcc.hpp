#pragma once

#include <cstdint>

namespace ESM
{
    inline constexpr std::uint32_t fourCC(const char (&name)[5])
    {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(name[0]) | (static_cast<std::uint8_t>(name[1]) << 8)
            | (static_cast<std::uint8_t>(name[2]) << 16) | (static_cast<std::uint8_t>(name[3]) << 24));
    }
}
