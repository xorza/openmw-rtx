#include "texturedata.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

namespace Rtx
{
    std::size_t MipPyramid::layOutTo1x1(const std::uint32_t width, const std::uint32_t height, const TexelLayout& laid)
    {
        mLevels.clear();

        if (width == 0 || height == 0)
            return 0;

        // Exactly, because a bake that grew this vector as it walked reached the heap once a level
        // — which is what `aScratchTheCallerKeepsAnswersForEachImageAndAllocatesForNone` counts.
        mLevels.reserve(levelsTo1x1(width, height));

        std::size_t bytes = 0;
        for (MipLevel level{ .mOffset = 0, .mWidth = width, .mHeight = height };;)
        {
            mLevels.push_back(level);
            bytes += laid.levelBytes(level.mWidth, level.mHeight);

            if (level.mWidth == 1 && level.mHeight == 1)
                break;

            assert(bytes <= std::numeric_limits<std::uint32_t>::max() && "a level laid out past a 32-bit offset");
            level = MipLevel{
                .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = std::max(level.mWidth / 2, 1u),
                .mHeight = std::max(level.mHeight / 2, 1u),
            };
        }

        return bytes;
    }

    std::size_t MipPyramid::layOutLike(const std::span<const MipLevel> shape, const TexelLayout& laid)
    {
        mLevels.clear();
        mLevels.reserve(shape.size());

        std::size_t bytes = 0;
        for (const MipLevel& level : shape)
        {
            assert(bytes <= std::numeric_limits<std::uint32_t>::max() && "a level laid out past a 32-bit offset");
            mLevels.push_back(MipLevel{
                .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = level.mWidth,
                .mHeight = level.mHeight,
            });

            bytes += laid.levelBytes(level.mWidth, level.mHeight);
        }

        return bytes;
    }

    bool TextureData::levelsFit() const
    {
        if (mLevels.empty())
            return true;

        const TexelLayout layout = layoutOf(mFormat);
        return std::ranges::all_of(mLevels, [&](const MipLevel& level) {
            return level.mOffset <= mBytes.size()
                && layout.levelBytes(level.mWidth, level.mHeight) <= mBytes.size() - level.mOffset;
        });
    }

    bool TextureData::wantsCompletedChain() const
    {
        return mLevels.size() == 1 && std::size_t{ mWidth } * mHeight > 1;
    }

    bool TextureData::encodesChain() const
    {
        return std::max(mWidth, mHeight) > sLargestLooseChainSide;
    }
}
