#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "textureformat.hpp"

namespace Rtx
{
    /// **One level of block-compressed texels as RGBA8, rows tight**, for a reader that shows them as
    /// they are: an interface image a mod ships compressed. The formats a file carries — BC1, BC2 and
    /// BC3, and as data BC4, read as grey, and BC5, as red and green — and the texels a level of
    /// `width` by `height` covers, the blocks past its edge cut off. False, writing nothing, for any
    /// other format, or blocks short of the level.
    bool decodeBlocks(TextureFormat format, std::span<const std::byte> blocks, std::uint32_t width,
        std::uint32_t height, std::span<std::uint8_t> into);
}
