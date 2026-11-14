#include "texels.hpp"

#include <algorithm>
#include <array>
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

    namespace
    {
        /// `walkBand` into a band of `level.mWidth` texels a row, each colour handed through `turn`.
        template <class Turn>
        void readBand(const TextureData& texture, const MipLevel& level, const std::uint32_t band,
            std::vector<osg::Vec3f>& into, Turn turn)
        {
            const std::uint32_t width = level.mWidth;
            into.resize(std::size_t{ bandRows(level, band) } * width);
            walkBand(
                texture, level, band,
                [&](const std::array<osg::Vec3f, 4>& stored) {
                    return std::array{ turn(stored[0]), turn(stored[1]), turn(stored[2]), turn(stored[3]) };
                },
                [&](std::uint32_t x, std::uint32_t row, const osg::Vec3f& turned) {
                    into[std::size_t{ row } * width + x] = turned;
                });
        }
    }

    void readTexelBand(
        const TextureData& texture, const MipLevel& level, const std::uint32_t band, std::vector<osg::Vec3f>& into)
    {
        readBand(texture, level, band, into, [](const osg::Vec3f& stored) { return stored; });
    }

    void readLightBand(
        const TextureData& texture, const MipLevel& level, const std::uint32_t band, std::vector<osg::Vec3f>& into)
    {
        readBand(texture, level, band, into, [](const osg::Vec3f& stored) { return toLinear(stored); });
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
            readLightBand(finest, level, first / 4, band);
            for (std::uint32_t y = first; y < first + band.size() / level.mWidth; ++y)
                for (std::uint32_t x = 0; x < level.mWidth; ++x)
                {
                    const osg::Vec3d light(band[std::size_t{ y - first } * level.mWidth + x]);
                    const double opacity = alpha.at(0, x, y) / 255.0;

                    total += light * opacity;
                    whole += light;
                    covered += opacity;
                }
        }

        const double texels = static_cast<double>(level.mWidth) * level.mHeight;
        total /= texels;
        whole /= texels;

        return MeanTexel{
            .mColour
            = osg::Vec3f(static_cast<float>(total.x()), static_cast<float>(total.y()), static_cast<float>(total.z())),
            .mWhole
            = osg::Vec3f(static_cast<float>(whole.x()), static_cast<float>(whole.y()), static_cast<float>(whole.z())),
            .mAlpha = static_cast<float>(covered / texels),
        };
    }
}
