#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <span>

#include <apps/rtxtool/instruments/png.hpp>

namespace RtxTool
{
    /// Every picture a run's stops write, kept as it was written, for the verb that judges them once the run is
    /// over: `shot --against` and `noise` read their own pictures here instead of decoding each file right after
    /// it was encoded. A run hands its stops a shelf only where its verb judges what they drew
    /// (`SessionRequest::mShelf`).
    ///
    /// Held until the shelf goes, on `PngImage`'s scale: 29 MB a picture at 2560x1440, so a noise suite of three
    /// places, five pictures each, holds 440 MB.
    class PictureShelf
    {
    public:
        /// Keeps the eight-bit picture written to `file`, as `readPng` would read it back.
        void keep(const std::filesystem::path& file, std::uint32_t width, std::uint32_t height,
            std::span<const std::uint8_t> pixels);

        /// Keeps the sixteen-bit picture written to `file`.
        void keep(const std::filesystem::path& file, std::uint32_t width, std::uint32_t height,
            std::span<const std::uint16_t> samples);

        /// The picture written to `file`, or null where none was: a write that failed, or a run that
        /// ended before the stop that writes it.
        const PngImage* find(const std::filesystem::path& file) const;

    private:
        std::map<std::filesystem::path, PngImage> mPictures;
    };
}
