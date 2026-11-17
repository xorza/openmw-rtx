#include "texelreads.hpp"

#include <components/resource/imageheader.hpp>

namespace Rtx
{
    TexelReads& TexelReads::operator+=(const TexelReads& other)
    {
        for (std::size_t at = 0; at < sTexelReadCount; ++at)
        {
            mCounts[at] += other.mCounts[at];
            mBytes[at] += other.mBytes[at];
        }
        mChainsFoundWhole += other.mChainsFoundWhole;
        return *this;
    }

    osg::ref_ptr<const osg::Image> readTexels(const osg::Image& image, const TexelRead why, TexelReads& reads)
    {
        osg::ref_ptr<const osg::Image> texels = Resource::texelsOf(image);
        if (texels.get() == &image)
            return texels;
        ++reads.mCounts[static_cast<std::size_t>(why)];
        reads.mBytes[static_cast<std::size_t>(why)] += texels->getTotalSizeInBytesIncludingMipmaps();
        return texels;
    }
}
