#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace Rtx::Testing
{
    /// The bytes of a DDS file in DXT1 (BC1) of `width` by `height`, multiples of four, carrying `levels` levels, each
    /// level's blocks every byte `fill`: 8 bytes a block. What a test hands a loader where a block file would arrive.
    inline std::string ddsDxt1(std::uint32_t width, std::uint32_t height, std::uint32_t levels, std::uint8_t fill)
    {
        std::string bytes(128, '\0');
        const auto put = [&](std::size_t at, std::uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); };
        put(0, 0x20534444u); // "DDS "
        put(4, 124u);
        put(8, 0x1u | 0x2u | 0x4u | 0x1000u | 0x20000u | 0x80000u); // caps, height, width, format, levels, linear size
        put(12, height);
        put(16, width);
        put(20, (width / 4) * (height / 4) * 8);
        put(28, levels);
        put(76, 32u);
        put(80, 0x4u); // a FourCC
        put(84, 0x31545844u); // "DXT1"
        put(108, 0x1000u | 0x400000u | 0x8u); // a texture, with levels
        for (std::uint32_t level = 0; level < levels; ++level)
        {
            const std::uint32_t across = std::max(1u, (width >> level) / 4);
            const std::uint32_t down = std::max(1u, (height >> level) / 4);
            bytes.append(std::size_t{ across } * down * 8, static_cast<char>(fill));
        }
        return bytes;
    }
}
