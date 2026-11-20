#pragma once

#include <algorithm>
#include <cstdint>

#include "texturedata.hpp"
#include "textureencoding.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    /// **What a texture's image is, as a device makes and prices it**: what makes it, its format, its sides, its
    /// levels, and what stands beside it. The one statement of a chain's shape the core plans by and a backend makes,
    /// so the two cannot disagree.
    struct ImageShape
    {
        /// What makes it: a file's levels uploaded, or a bake the device writes.
        TextureSource mSource = TextureSource::File;
        TextureFormat mFormat = TextureFormat::Unnamed;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
        std::uint8_t mLevels = 0;
        TextureCompanion mCompanion = TextureCompanion::Neutral;

        /// The chain from `level` down: what stands of it from that level.
        ImageShape from(const std::uint8_t level) const
        {
            ImageShape at = *this;
            at.mWidth = std::max(mWidth >> level, 1u);
            at.mHeight = std::max(mHeight >> level, 1u);
            at.mLevels = static_cast<std::uint8_t>(mLevels - level);
            return at;
        }

        friend bool operator==(const ImageShape&, const ImageShape&) = default;
    };
}
