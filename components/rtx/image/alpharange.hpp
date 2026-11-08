#pragma once

#include <cstdint>

namespace Rtx
{
    /// The lowest and the highest alpha a texture's finest level holds, a byte each as the file stores
    /// them: the heights a normal map's relief spans (`alphaRange`). The whole byte range where nothing
    /// was read, which bounds every texel.
    struct AlphaRange
    {
        std::uint8_t mLow = 0;
        std::uint8_t mHigh = 255;

        /// Whether a texel stands higher than another anywhere: a flat map has no relief to march.
        constexpr bool spans() const { return mHigh > mLow; }

        bool operator==(const AlphaRange& other) const = default;
    };
}
