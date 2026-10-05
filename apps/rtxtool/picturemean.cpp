#include "picturemean.hpp"

#include <cassert>
#include <cstddef>

#include <components/rtx/renderer/png.hpp>

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

    void PictureMean::mean(std::vector<std::uint16_t>& samples) const
    {
        assert(mCount > 0 && "the mean of no pictures");

        samples.resize(mSums.size());
        for (std::size_t at = 0; at < mSums.size(); ++at)
            samples[at] = static_cast<std::uint16_t>(
                (std::uint64_t{ mSums[at] } * 2 * Rtx::sSamplesPerLevel + mCount) / (std::uint64_t{ 2 } * mCount));
    }

    void PictureMean::clear()
    {
        mSums.clear();
        mWidth = 0;
        mHeight = 0;
        mCount = 0;
    }
}
