#pragma once

#include <cstdint>

namespace MWBase
{
    using RotationFlags = std::uint16_t;

    enum RotationFlag : RotationFlags
    {
        RotationFlag_none = 0,
        RotationFlag_adjust = 1,
        RotationFlag_inverseOrder = 1 << 1,
    };
}
