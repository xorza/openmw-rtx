#include "madeimage.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <utility>

namespace Rtx
{
    std::size_t MadeLayout::bytes() const
    {
        const TexelLayout laid = layoutOf(mFormat);
        std::size_t bytes = 0;
        for (std::uint32_t level = 0; level < mCount; ++level)
            bytes += laid.levelBytes(std::max(mWidth >> level, 1u), std::max(mHeight >> level, 1u));
        return bytes;
    }

    osg::ref_ptr<const MadeImage> MadeImage::make(const MadeLayout& layout, Misc::ByteBuffer bytes)
    {
        assert(layout.mCount > 0 && "a made chain of no level");
        assert(layout.mCount <= levelsTo1x1(layout.mWidth, layout.mHeight) && "a level under a texel");
        assert(bytes.size() == layout.bytes() && "a chain of another length than its levels");
        assert(bytes.size() <= std::numeric_limits<std::uint32_t>::max() && "a level past a 32-bit offset");

        osg::ref_ptr<MadeImage> made = new MadeImage;
        made->mFormat = layout.mFormat;
        made->mBytes = std::move(bytes);
        made->mLaid = layoutOf(layout.mFormat);
        made->mLevels.reserve(layout.mCount);
        std::size_t offset = 0;
        for (std::uint32_t level = 0; level < layout.mCount; ++level)
        {
            const MipLevel at{ .mOffset = static_cast<std::uint32_t>(offset),
                .mWidth = std::max(layout.mWidth >> level, 1u),
                .mHeight = std::max(layout.mHeight >> level, 1u) };
            made->mLevels.push_back(at);
            offset += made->mLaid.levelBytes(at.mWidth, at.mHeight);
        }
        return made;
    }
}
