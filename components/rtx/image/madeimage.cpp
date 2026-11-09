#include "madeimage.hpp"

#include <algorithm>
#include <cassert>
#include <utility>

namespace Rtx
{
    MadeImage::MadeImage(const osg::Image& file, const TextureFormat format, Misc::ByteBuffer coarser)
        : mBytes(std::move(coarser))
    {
        assert(!isWidened(format) && "a widened file's chain is made whole");
        layLevels(static_cast<std::uint32_t>(file.s()), static_cast<std::uint32_t>(file.t()), 1, layoutOf(format));
    }

    osg::ref_ptr<const MadeImage> MadeImage::whole(
        const TextureFormat format, const std::uint32_t width, const std::uint32_t height, Misc::ByteBuffer levels)
    {
        osg::ref_ptr<MadeImage> made = new MadeImage;
        made->mWholeFormat = format;
        made->mBytes = std::move(levels);
        made->layLevels(width, height, 0, layoutOf(format));
        return made;
    }

    void MadeImage::layLevels(
        const std::uint32_t width, const std::uint32_t height, const std::uint32_t first, const TexelLayout& laid)
    {
        mLaid = laid;
        std::size_t bytes = 0;
        for (std::uint32_t level = first; bytes < mBytes.size(); ++level)
        {
            const MipLevel at{ .mOffset = static_cast<std::uint32_t>(bytes),
                .mWidth = std::max(width >> level, 1u),
                .mHeight = std::max(height >> level, 1u) };
            mLevels.push_back(at);
            bytes += laid.levelBytes(at.mWidth, at.mHeight);
        }
        assert(bytes == mBytes.size() && "a chain of another length than its levels");
    }
}
