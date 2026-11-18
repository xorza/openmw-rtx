#pragma once

#include <cstdint>

namespace MWWorld
{
    using MovementDirectionFlags = std::uint8_t;

    enum MovementDirectionFlag : MovementDirectionFlags
    {
        MovementDirectionFlag_Forward = 1 << 0,
        MovementDirectionFlag_Back = 1 << 1,
        MovementDirectionFlag_Left = 1 << 2,
        MovementDirectionFlag_Right = 1 << 3,
    };
}
