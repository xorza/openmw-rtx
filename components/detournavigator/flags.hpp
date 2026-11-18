#pragma once

#include <cstdint>

namespace DetourNavigator
{
    using Flags = std::uint16_t;

    enum Flag : Flags
    {
        Flag_none = 0,
        Flag_walk = 1 << 0,
        Flag_swim = 1 << 1,
        Flag_openDoor = 1 << 2,
        Flag_usePathgrid = 1 << 3,
    };
}
