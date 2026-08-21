#include "contactsheet.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include <components/rtx/colourblock.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/texturedata.hpp>

#include <components/rtxbridge/png.hpp>

namespace RtxTool
{
    namespace
    {
        /// How large one thumbnail is drawn, and how far apart the pairs stand.
        constexpr std::uint32_t sThumbnail = 128;
        constexpr std::uint32_t sGap = 6;

        /// How many pairs stand across the sheet.
        constexpr std::uint32_t sColumns = 6;

        std::uint32_t blockBytes(Rtx::TextureFormat format)
        {
            switch (format)
            {
                case Rtx::TextureFormat::Bc1RgbaSrgb:
                    return 8;
                case Rtx::TextureFormat::Bc2Srgb:
                case Rtx::TextureFormat::Bc3Srgb:
                    return 16;
                case Rtx::TextureFormat::Rgba8Unorm:
                    return 0;
            }

            throw Rtx::Error("unknown texture format");
        }

        /// One texel of a texture's largest level, as it is stored.
        ///
        /// Read a texel at a time rather than decoded into a buffer first: a thumbnail asks for one
        /// texel in a few hundred, so decoding the whole level would be most of the work for none of
        /// the answer.
        osg::Vec3f texelAt(const Rtx::TextureData& texture, std::uint32_t x, std::uint32_t y)
        {
            const Rtx::MipLevel& level = texture.mLevels.front();
            const std::uint32_t bytes = blockBytes(texture.mFormat);

            if (bytes == 0)
            {
                const std::size_t at = level.mOffset + (std::size_t{ y } * level.mWidth + x) * 4;
                const auto channel = [&](std::size_t offset) {
                    return std::to_integer<std::uint32_t>(texture.mBytes[at + offset]) / 255.0f;
                };

                return osg::Vec3f(channel(0), channel(1), channel(2));
            }

            const std::uint32_t columns = (level.mWidth + 3) / 4;
            const std::size_t at = level.mOffset + (std::size_t{ y / 4 } * columns + x / 4) * bytes + (bytes - 8);
            const Rtx::ColourBlock block = Rtx::ColourBlock::read(
                texture.mBytes.subspan(at).first<8>(), texture.mFormat == Rtx::TextureFormat::Bc1RgbaSrgb);

            const std::size_t texel = std::size_t{ y % 4 } * 4 + x % 4;
            return block.mPalette[block.indexAt(texel)];
        }

        float toLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        float toEncoded(float linear)
        {
            const float value = linear <= 0.0031308f ? linear * 12.92f
                                                     : 1.055f * std::pow(std::max(linear, 0.0f), 1.0f / 2.4f) - 0.055f;
            return std::clamp(value, 0.0f, 1.0f);
        }

        /// The map at a point, bilinear and wrapping — the shader's `paintedLight`, in C++.
        float paintedLight(std::span<const float> map, float u, float v)
        {
            constexpr int extent = static_cast<int>(Rtx::Shaders::SHADING_EXTENT);
            const auto fraction = [](float value) { return value - std::floor(value); };

            const float x = fraction(u) * extent - 0.5f;
            const float y = fraction(v) * extent - 0.5f;
            const int lowX = static_cast<int>(std::floor(x));
            const int lowY = static_cast<int>(std::floor(y));
            const float acrossX = x - static_cast<float>(lowX);
            const float acrossY = y - static_cast<float>(lowY);

            const auto wrap = [](int at) { return (at % extent + extent) % extent; };
            const auto cell = [&](int column, int row) {
                return map[static_cast<std::size_t>(wrap(row)) * extent + static_cast<std::size_t>(wrap(column))];
            };

            const float top = std::lerp(cell(lowX, lowY), cell(lowX + 1, lowY), acrossX);
            const float bottom = std::lerp(cell(lowX, lowY + 1), cell(lowX + 1, lowY + 1), acrossX);
            return std::lerp(top, bottom, acrossY);
        }
    }

    std::uint32_t ContactSheet::getStride()
    {
        return sThumbnail + sGap;
    }

    std::uint32_t ContactSheet::getThumbnail()
    {
        return sThumbnail;
    }

    std::uint32_t ContactSheet::getLeftOf(std::uint32_t index) const
    {
        return sGap + index % sColumns * (sThumbnail * 2 + sGap + sGap);
    }

    std::uint32_t ContactSheet::getTopOf(std::uint32_t index) const
    {
        return sGap + index / sColumns * (sThumbnail + sGap);
    }

    ContactSheet drawContactSheet(std::span<const Rtx::TextureData> textures, float strength)
    {
        if (textures.empty())
            return ContactSheet{};

        const auto count = static_cast<std::uint32_t>(textures.size());
        const std::uint32_t rows = (count + sColumns - 1) / sColumns;
        const std::uint32_t pairWidth = sThumbnail * 2 + sGap;
        const std::uint32_t width = sColumns * pairWidth + (sColumns + 1) * sGap;
        const std::uint32_t height = rows * (sThumbnail + sGap) + sGap;

        // Mid grey behind them, so a texture that is black and one that is missing do not look the
        // same as the paper they are printed on.
        std::vector<std::uint8_t> sheet(std::size_t{ width } * height * 4, std::uint8_t{ 64 });
        for (std::size_t at = 3; at < sheet.size(); at += 4)
            sheet[at] = 255;

        ContactSheet drawn{ std::move(sheet), width, height, count };
        for (std::uint32_t index = 0; index < count; ++index)
        {
            const Rtx::TextureData& texture = textures[index];
            const std::uint32_t left = drawn.getLeftOf(index);
            const std::uint32_t top = drawn.getTopOf(index);

            for (std::uint32_t y = 0; y < sThumbnail; ++y)
                for (std::uint32_t x = 0; x < sThumbnail; ++x)
                {
                    // Nearest neighbour, and the centre of the texel a thumbnail pixel covers.
                    const float u = (x + 0.5f) / sThumbnail;
                    const float v = (y + 0.5f) / sThumbnail;
                    const auto texelX = std::min(
                        static_cast<std::uint32_t>(u * static_cast<float>(texture.mWidth)), texture.mWidth - 1);
                    const auto texelY = std::min(
                        static_cast<std::uint32_t>(v * static_cast<float>(texture.mHeight)), texture.mHeight - 1);

                    const osg::Vec3f stored = texelAt(texture, texelX, texelY);
                    osg::Vec3f corrected = stored;
                    if (!texture.mShading.empty() && strength > 0.0f)
                    {
                        // **In linear, because that is where the shader divides.** A texture's
                        // bytes are display-encoded and the sampler hands the frame linear values,
                        // so a sheet that divided the bytes would be showing a correction the
                        // renderer never applies — half again too strong in the darks.
                        const float factor = std::lerp(1.0f, paintedLight(texture.mShading, u, v), strength);
                        const bool srgb = Rtx::isSrgb(texture.mFormat);
                        for (int channel = 0; channel < 3; ++channel)
                        {
                            const float linear = srgb ? toLinear(stored[channel]) : stored[channel];
                            corrected[channel] = srgb ? toEncoded(linear / factor) : linear / factor;
                        }
                    }

                    const auto place = [&](std::uint32_t offset, const osg::Vec3f& colour) {
                        const std::size_t at = (std::size_t{ top + y } * width + left + offset + x) * 4;
                        for (int channel = 0; channel < 3; ++channel)
                            drawn.mPixels[at + channel] = static_cast<std::uint8_t>(
                                std::lround(std::clamp(colour[channel], 0.0f, 1.0f) * 255.0f));
                    };

                    place(0, stored);
                    place(ContactSheet::getStride(), corrected);
                }
        }

        return drawn;
    }

    ContactSheet writeContactSheet(
        std::span<const Rtx::TextureData> textures, const std::filesystem::path& out, float strength)
    {
        ContactSheet sheet = drawContactSheet(textures, strength);
        if (sheet.mCount > 0)
            RtxBridge::writePng(out, sheet.mWidth, sheet.mHeight, sheet.mPixels);

        return sheet;
    }
}
