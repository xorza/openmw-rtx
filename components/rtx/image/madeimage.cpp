#include "madeimage.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace Rtx
{
    std::size_t MadeLayout::offsetOf(const std::uint32_t level) const
    {
        assert(level <= mCount && "an offset past the chain's end");
        const TexelLayout laid = layoutOf(mFormat);
        std::size_t bytes = 0;
        for (std::uint32_t at = 0; at < level; ++at)
            bytes += laid.levelBytes(std::max(mWidth >> at, 1u), std::max(mHeight >> at, 1u));
        return bytes;
    }

    std::size_t MadeLayout::bytes() const
    {
        return offsetOf(mCount) - offsetOf(mFirst);
    }

    osg::ref_ptr<const MadeImage> MadeImage::make(const MadeLayout& layout, PooledBuffer bytes)
    {
        assert(layout.mCount > 0 && "a made chain of no level");
        assert(layout.mFirst < layout.mCount && "a chain held from past its last level");
        assert(layout.mCount <= levelsTo1x1(layout.mWidth, layout.mHeight) && "a level under a texel");
        assert(bytes.bytes().size() == layout.bytes() && "a chain of another length than its levels");
        assert(bytes.bytes().size() <= std::numeric_limits<std::uint32_t>::max() && "a level past a 32-bit offset");

        osg::ref_ptr<MadeImage> made = new MadeImage;
        made->mFormat = layout.mFormat;
        made->mBytes = std::move(bytes);
        made->mLaid = layoutOf(layout.mFormat);
        made->mLevels.reserve(layout.mCount);
        std::size_t offset = 0;
        for (std::uint32_t level = 0; level < layout.mCount; ++level)
        {
            const bool held = level >= layout.mFirst;
            const MipLevel at{ .mOffset = static_cast<std::uint32_t>(held ? offset : 0),
                .mWidth = std::max(layout.mWidth >> level, 1u),
                .mHeight = std::max(layout.mHeight >> level, 1u) };
            made->mLevels.push_back(at);
            if (held)
                offset += made->mLaid.levelBytes(at.mWidth, at.mHeight);
        }
        return made;
    }
}
