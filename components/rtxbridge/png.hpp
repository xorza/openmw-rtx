#pragma once

#include <cstdint>
#include <filesystem>
#include <span>

namespace RtxBridge
{
    /// Writes tightly packed 8-bit RGBA, top row first, as a PNG.
    ///
    /// The renderer writes row zero at the top and OSG's images start at the bottom, so this flips
    /// on the way through. Throws when the file cannot be written.
    void writePng(const std::filesystem::path& path, std::uint32_t width, std::uint32_t height,
        std::span<const std::uint8_t> pixels);
}
