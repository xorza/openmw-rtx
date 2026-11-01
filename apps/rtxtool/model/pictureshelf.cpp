#include "pictureshelf.hpp"

#include <cassert>
#include <cstddef>

namespace RtxTool
{
    void PictureShelf::keep(const std::filesystem::path& file, const std::uint32_t width, const std::uint32_t height,
        const std::span<const std::uint8_t> pixels)
    {
        assert(pixels.size() == std::size_t{ width } * height * 4 && "a picture of another size than it says");
        PngImage& kept = mPictures[file];
        kept.mWidth = width;
        kept.mHeight = height;
        kept.mSamples.resize(pixels.size());
        for (std::size_t at = 0; at < pixels.size(); ++at)
            kept.mSamples[at] = static_cast<std::uint16_t>(pixels[at] * sSamplesPerLevel);
    }

    void PictureShelf::keep(const std::filesystem::path& file, const std::uint32_t width, const std::uint32_t height,
        const std::span<const std::uint16_t> samples)
    {
        assert(samples.size() == std::size_t{ width } * height * 4 && "a picture of another size than it says");
        mPictures[file]
            = PngImage{ .mWidth = width, .mHeight = height, .mSamples = { samples.begin(), samples.end() } };
    }

    const PngImage* PictureShelf::find(const std::filesystem::path& file) const
    {
        const auto found = mPictures.find(file);
        return found == mPictures.end() ? nullptr : &found->second;
    }
}
