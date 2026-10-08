#include "texels.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <osg/Vec3d>

#include "alphaimage.hpp"
#include "colour.hpp"
#include "colourblock.hpp"

namespace Rtx
{
    namespace
    {
        osg::Vec3f looseTexel(const TextureData& texture, const MipLevel& level, const TexelLayout& layout,
            const std::uint32_t x, const std::uint32_t y)
        {
            return looseColourAt(texture, level.texelOffset(x, y, layout.mBytes));
        }

        /// The block at `column` and `band`, counted in blocks.
        ColourBlock colourBlockAt(const TextureData& texture, const MipLevel& level, const TexelLayout& layout,
            const std::uint32_t column, const std::uint32_t band)
        {
            return ColourBlock::read(
                colourHalfAt(texture.mBytes, level.blockOffset(column, band, layout.mBytes), layout),
                isBc1(texture.mFormat));
        }
    }

    std::span<const std::byte, 8> colourHalfAt(
        const std::span<const std::byte> bytes, const std::size_t block, const TexelLayout& layout)
    {
        return bytes.subspan(block + (layout.mBytes - 8)).first<8>();
    }

    osg::Vec3f looseColourAt(const TextureData& texture, const std::size_t at)
    {
        assert(layoutOf(texture.mFormat).mBytes == 4 && "a loose texel read as four bytes that is not");
        const auto channel
            = [&](std::size_t offset) { return std::to_integer<std::uint32_t>(texture.mBytes[at + offset]) / 255.0f; };

        // The two loose spellings differ only in which end the three colours are stated from.
        if (isBgr(texture.mFormat))
            return osg::Vec3f(channel(2), channel(1), channel(0));

        return osg::Vec3f(channel(0), channel(1), channel(2));
    }

    bool readsColour(const TextureData& texture)
    {
        return !texture.mBytes.empty() && !texture.mLevels.empty() && traitsOf(texture.mFormat).mColour;
    }

    osg::Vec3f texelAt(const TextureData& texture, const MipLevel& level, std::uint32_t x, std::uint32_t y)
    {
        assert(readsColour(texture) && "a texel read of a texture with no colour to read");
        assert(texture.levelsFit() && "a texel read of a description short of its bytes");
        assert(x < level.mWidth && y < level.mHeight);

        const TexelLayout layout = layoutOf(texture.mFormat);
        if (!layout.isBlocked())
            return looseTexel(texture, level, layout, x, y);

        const ColourBlock block = colourBlockAt(texture, level, layout, x / 4, y / 4);
        return block.mPalette[block.indexAt(std::size_t{ y % 4 } * 4 + x % 4)];
    }

    void readTexelBand(
        const TextureData& texture, const MipLevel& level, const std::uint32_t band, std::vector<osg::Vec3f>& into)
    {
        assert(readsColour(texture) && "a band read of a texture with no colour to read");
        assert(texture.levelsFit() && "a band read of a description short of its bytes");
        const std::uint32_t first = band * 4;
        assert(first < level.mHeight && "a band below the level");
        const std::uint32_t rows = std::min(level.mHeight - first, 4u);
        const std::uint32_t width = level.mWidth;
        into.resize(std::size_t{ rows } * width);

        const TexelLayout layout = layoutOf(texture.mFormat);
        if (!layout.isBlocked())
        {
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t x = 0; x < width; ++x)
                    into[std::size_t{ row } * width + x] = looseTexel(texture, level, layout, x, first + row);
            return;
        }

        const std::uint32_t columns = (width + 3) / 4;
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            const ColourBlock block = colourBlockAt(texture, level, layout, column, band);

            const std::uint32_t across = std::min(width - column * 4, 4u);
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t x = 0; x < across; ++x)
                    into[std::size_t{ row } * width + column * 4 + x]
                        = block.mPalette[block.indexAt(std::size_t{ row } * 4 + x)];
        }
    }

    osg::Vec3f MeanTexel::opaque() const
    {
        if (!(mAlpha > 0.0f))
            return osg::Vec3f();

        return mColour / mAlpha;
    }

    MeanTexel meanTexel(const TextureData& finest, TexelScratch& scratch)
    {
        const MipLevel& level = finest.mLevels.front();

        // The alpha is never empty here, because it is empty only for a description carrying no
        // texels — so it is read rather than defaulted, which is the difference between a star
        // sheet worth nearly nothing and one worth the black it is painted on.
        AlphaImage& alpha = scratch.mAlpha;
        alpha.build(finest);

        // Row by row, in the order a texel at a time was summed, so the means are the same to the
        // bit; a band at a time, so a block is decoded once and not once a texel.
        std::vector<osg::Vec3f>& band = scratch.mColours;
        osg::Vec3d total;
        osg::Vec3d whole;
        double covered = 0.0;
        for (std::uint32_t first = 0; first < level.mHeight; first += 4)
        {
            readTexelBand(finest, level, first / 4, band);
            for (std::uint32_t y = first; y < first + band.size() / level.mWidth; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const osg::Vec3d light(toLinear(band[std::size_t{ y - first } * level.mWidth + x]));
                    const double opacity = alpha.at(0, x, y) / 255.0;

                    total += light * opacity;
                    whole += light;
                    covered += opacity;
                }
        }

        const double texels = double(level.mWidth) * level.mHeight;
        total /= texels;
        whole /= texels;

        return MeanTexel{
            .mColour = osg::Vec3f(float(total.x()), float(total.y()), float(total.z())),
            .mWhole = osg::Vec3f(float(whole.x()), float(whole.y()), float(whole.z())),
            .mAlpha = float(covered / texels),
        };
    }
}
