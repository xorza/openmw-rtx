#pragma once

#include <bit>
#include <cstdint>

namespace Rtx
{
    /// One half float, as the number it stands for. By bits, where the test harness spells the
    /// same conversion out by arithmetic, so that each derivation checks the other.
    inline float fromHalf(std::uint16_t bits)
    {
        const std::uint32_t sign = static_cast<std::uint32_t>(bits & 0x8000u) << 16;
        const std::uint32_t exponent = (bits >> 10) & 0x1fu;
        const std::uint32_t mantissa = bits & 0x3ffu;

        if (exponent == 31)
            return std::bit_cast<float>(sign | 0x7f800000u | (mantissa << 13));

        // A subnormal half is its mantissa times 2^-24, and the float it widens to is normal —
        // so the shuffle below cannot make it and a multiply is what does.
        if (exponent == 0)
        {
            const float magnitude = static_cast<float>(mantissa) * 0x1p-24f;

            return (bits & 0x8000u) != 0 ? -magnitude : magnitude;
        }

        // Bias 15 to bias 127, and ten mantissa bits to twenty-three.
        return std::bit_cast<float>(sign | ((exponent + 112u) << 23) | (mantissa << 13));
    }
}
