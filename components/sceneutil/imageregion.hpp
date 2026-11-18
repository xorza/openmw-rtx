#pragma once

#include <algorithm>
#include <cstdint>

namespace SceneUtil
{
    /// A rectangle of an image, in texels from its corner: row nought first, as `osg::Image` counts
    /// rows. Named, because the functions that take two of them — a source and a target — would
    /// take a transposed pair without a word.
    struct ImageRegion
    {
        std::int32_t mX = 0;
        std::int32_t mY = 0;
        std::int32_t mWidth = 0;
        std::int32_t mHeight = 0;

        bool empty() const { return mWidth <= 0 || mHeight <= 0; }

        /// The smallest rectangle holding both. Either empty one is the other.
        ImageRegion joined(const ImageRegion& other) const
        {
            if (empty())
                return other;
            if (other.empty())
                return *this;

            const std::int32_t left = std::min(mX, other.mX);
            const std::int32_t bottom = std::min(mY, other.mY);
            const std::int32_t right = std::max(mX + mWidth, other.mX + other.mWidth);
            const std::int32_t top = std::max(mY + mHeight, other.mY + other.mHeight);
            return ImageRegion{ left, bottom, right - left, top - bottom };
        }

        bool operator==(const ImageRegion&) const = default;
    };
}
