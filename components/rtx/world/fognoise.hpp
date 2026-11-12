#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    /// How many texels `level` of the fog's field is a side: the tile halved that many times, and
    /// never below one.
    constexpr std::uint32_t fogFieldSideAt(std::uint32_t level)
    {
        return std::max(Shaders::FOG_FIELD_SIZE >> level, 1u);
    }

    /// Where `level` begins in the field's bytes, two channels a texel and every level before it whole.
    constexpr std::size_t fogFieldOffsetAt(std::uint32_t level)
    {
        std::size_t offset = 0;
        for (std::uint32_t before = 0; before < level; ++before)
            offset += std::size_t{ 2 } * fogFieldSideAt(before) * fogFieldSideAt(before) * fogFieldSideAt(before);
        return offset;
    }

    /// How many bytes the field holds, every level end to end.
    inline constexpr std::size_t sFogFieldBytes = fogFieldOffsetAt(Shaders::FOG_FIELD_LEVELS);

    /// A fog field as the device takes it: every level end to end, the full one first, two channels a
    /// texel, slice by slice (`fogFieldOffsetAt`).
    using FogField = std::span<const std::uint8_t, sFogFieldBytes>;

    /// The fog's fractal field, drawn into a wrapping volume with a chain of levels under it, so a
    /// sampler reads it instead of eight hashes an octave at every march step. One octave, wrapping on
    /// all three axes; the shader builds the fractal from three scales. Every level is normalised to one
    /// mean and spread, so a coarse step loses the detail and never the amount of air. Made by the build
    /// (`makeFogNoise`) and compiled in, so no start spends the 53 ms it takes.
    extern const std::array<std::uint8_t, sFogFieldBytes> sFogNoise;
}
