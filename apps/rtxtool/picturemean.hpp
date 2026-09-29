#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace RtxTool
{
    /// A running mean of pictures of one size, four bytes a pixel, byte for byte: what `noise`
    /// averages a frame's independent draws into, which is the picture that frame converges to.
    ///
    /// **In the bytes the picture is shown in**, because what `noise` asks of a frame is how far
    /// what a player sees stands from what it would settle to, and the display curve stands between
    /// the radiance and that.
    class PictureMean
    {
    public:
        /// Adds one picture. The first sets the size, and every later one has it.
        void add(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height);

        /// How many pictures are in it.
        std::uint32_t getCount() const { return mCount; }

        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        /// The mean so far into `pixels`, each byte rounded to the nearest, a half up. There is at
        /// least one picture in it.
        void mean(std::vector<std::uint8_t>& pixels) const;

        /// Empties it for another mean, keeping the room the sums took.
        void clear();

    private:
        std::vector<std::uint32_t> mSums;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint32_t mCount = 0;
    };
}
