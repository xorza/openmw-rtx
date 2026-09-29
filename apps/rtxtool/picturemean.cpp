#include "picturemean.hpp"

#include <cassert>
#include <cstddef>

namespace RtxTool
{
    void PictureMean::add(std::span<const std::uint8_t> pixels, std::uint32_t width, std::uint32_t height)
    {
        assert(pixels.size() == std::size_t{ width } * height * 4 && "a picture that is not its size");
        if (mCount == 0)
        {
            mWidth = width;
            mHeight = height;
            mSums.assign(pixels.size(), 0);
        }
        assert(width == mWidth && height == mHeight && "a mean of pictures of two sizes");

        for (std::size_t at = 0; at < pixels.size(); ++at)
            mSums[at] += pixels[at];
        ++mCount;
    }

    void PictureMean::mean(std::vector<std::uint8_t>& pixels) const
    {
        assert(mCount > 0 && "the mean of no pictures");

        pixels.resize(mSums.size());
        for (std::size_t at = 0; at < mSums.size(); ++at)
            pixels[at] = static_cast<std::uint8_t>((2 * mSums[at] + mCount) / (2 * mCount));
    }

    void PictureMean::clear()
    {
        mSums.clear();
        mWidth = 0;
        mHeight = 0;
        mCount = 0;
    }
}
