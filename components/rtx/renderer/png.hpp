#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/misc/result.hpp>

namespace Rtx
{
    /// Writes tightly packed 8-bit RGBA, top row first, as a PNG. The renderer writes row zero at
    /// the top and OSG's images start at the bottom, so this flips on the way through. Says why where
    /// the file could not be written.
    ///
    /// `description`, where it is not empty, travels inside the file as its `Description`, UTF-8
    /// in an `iTXt` chunk: what an image viewer's properties and `exiftool` show, so a picture
    /// carries what it is of wherever it is copied.
    Misc::Result<void, std::string> writePng(const std::filesystem::path& path, std::uint32_t width,
        std::uint32_t height, std::span<const std::uint8_t> pixels, std::string_view description = {});

    /// A picture in the layout `writePng` takes: tightly packed 8-bit RGBA, top row first.
    struct PngImage
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint8_t> mPixels;
    };

    /// Reads a PNG back into that layout, or says why it could not: the file is missing, does not
    /// decode, or is not eight-bit colour — what a caller comparing two runs reports rather than
    /// throws over.
    Misc::Result<PngImage, std::string> readPng(const std::filesystem::path& path);
}
