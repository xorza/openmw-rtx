#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <gtest/gtest.h>

#include <components/rtx/image/textureformat.hpp>

namespace Rtx::Testing
{
    /// One texel's four bytes, in the order its format stores them.
    using Texel = std::array<std::uint8_t, 4>;

    /// The first texel of a level's `bytes` in `format`, as a device decodes it: a BC7 or a BC1 block's
    /// first, and the first four bytes as they stand in any other format.
    Texel firstTexelOf(TextureFormat format, std::span<const std::byte> bytes);

    /// Whether every channel of `got` is within `step` of `want`: a block holds one colour to its
    /// endpoints' precision, and a test of a made chain names the colour and that step.
    ::testing::AssertionResult isNear(const Texel& got, const Texel& want, int step);
}
