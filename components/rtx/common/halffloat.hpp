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

    /// The half float nearest `value`, ties to the even one: what the device's `packHalf2x16` makes
    /// where it rounds as the specification's default, and what `fromHalf` reads back exactly. An
    /// infinity stays one, past the largest half becomes one, and a NaN stays a NaN.
    inline std::uint16_t toHalf(float value)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        const auto sign = static_cast<std::uint16_t>((bits >> 16) & 0x8000u);
        const std::uint32_t magnitude = bits & 0x7fffffffu;

        if (magnitude > 0x7f800000u)
            return static_cast<std::uint16_t>(sign | 0x7e00u);

        // 65520 is half way between the largest half, 65504, and the next step past it, so it and
        // everything above round to the infinity.
        if (magnitude >= 0x477ff000u)
            return static_cast<std::uint16_t>(sign | 0x7c00u);

        // Under half the smallest subnormal, 2^-25, is nought; half of it exactly is a tie, which
        // the even neighbour, nought, takes.
        if (magnitude <= 0x33000000u)
            return sign;

        const std::uint32_t exponent = magnitude >> 23;
        const std::uint32_t mantissa = (magnitude & 0x7fffffu) | 0x800000u;

        // Rounded to the nearest of `units`, ties to the even one. A carry out of the mantissa steps
        // the exponent, which is the right answer: a half's bits order as its values do.
        const auto rounded = [](const std::uint32_t units, const std::uint32_t remainder, const std::uint32_t halfway) {
            return units + ((remainder > halfway || (remainder == halfway && (units & 1u) != 0)) ? 1u : 0u);
        };

        // Below the smallest normal half, 2^-14: a count of 2^-24 steps.
        if (exponent < 113)
        {
            const std::uint32_t shift = 126 - exponent;
            const std::uint32_t units = mantissa >> shift;
            const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
            return static_cast<std::uint16_t>(sign | rounded(units, remainder, 1u << (shift - 1u)));
        }

        // Bias 127 to bias 15, and twenty-three mantissa bits to ten.
        const std::uint32_t units = ((exponent - 112u) << 10) | ((mantissa & 0x7fffffu) >> 13);
        return static_cast<std::uint16_t>(sign | rounded(units, mantissa & 0x1fffu, 0x1000u));
    }
}
