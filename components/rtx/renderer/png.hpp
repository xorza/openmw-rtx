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

    /// Writes tightly packed RGBA at sixteen bits a channel, top row first, as a sixteen-bit PNG,
    /// and otherwise as the eight-bit `writePng`: what a mean of pictures is written as, since it
    /// falls between the levels a byte holds, and rounded to a byte it moves a measure of noise by
    /// up to half a level a pixel.
    Misc::Result<void, std::string> writePng(const std::filesystem::path& path, std::uint32_t width,
        std::uint32_t height, std::span<const std::uint16_t> samples, std::string_view description = {});

    /// A picture read back, of either depth: tightly packed RGBA, top row first, every channel on
    /// the sixteen-bit scale. **An eight-bit level `v` reads as `257 v`**, which maps nought to 255
    /// onto nought to 65535 exactly, so a picture and a mean of pictures compare on one scale.
    struct PngImage
    {
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::vector<std::uint16_t> mSamples;
    };

    /// What one eight-bit level is on `PngImage`'s scale.
    inline constexpr std::uint32_t sSamplesPerLevel = 257;

    /// Reads a PNG back into that layout, or says why it could not: the file is missing, does not
    /// decode, or is not eight- or sixteen-bit colour — what a caller comparing two runs reports
    /// rather than throws over.
    Misc::Result<PngImage, std::string> readPng(const std::filesystem::path& path);
}
