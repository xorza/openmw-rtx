#pragma once

#include <array>
#include <cstddef>

#include <components/rtx/shaders/scene.h>

namespace Rtx
{
    /// How many values the blue noise tile holds: `RANDOM_STREAMS` a pixel.
    inline constexpr std::size_t sBlueNoiseValues
        = std::size_t{ Shaders::BLUE_NOISE_EXTENT } * Shaders::BLUE_NOISE_EXTENT * Shaders::RANDOM_STREAMS;

    /// A square tile of blue noise: `RANDOM_STREAMS` independent masks over the same pixels. Blue
    /// noise does not make a sample better, it pushes the error into the high frequencies, which
    /// a filter takes out cleanly where a hash's white error leaves blotches no filter can tell
    /// from smooth lighting. Made by the build (`makeBlueNoise`) and compiled in, so no start
    /// spends the tenth of a second it takes.
    ///
    /// Values in `[0, 1)`, one pixel's `RANDOM_STREAMS` channels at a time, row by row.
    /// Interleaved rather than planar, so that the channels one pixel reads sit in one cache
    /// line: a shader that draws a pair touches two of them and never the same channel twice.
    extern const std::array<float, sBlueNoiseValues> sBlueNoise;
}
