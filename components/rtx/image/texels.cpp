#include "texels.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC and RGTC formats, which Windows's and Apple's gl.h lack.
#include <osg/Vec3d>

#include <components/sceneutil/util.hpp>

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

    MeanTexel meanTexel(const TextureData& finest, AlphaScratch& scratch)
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

    namespace
    {
        /// A pixel format and a data type a loaded image names, and the format each encoding reads
        /// it as. **A loose format is the two together**: the pixel format says which channels and
        /// the data type how many bits they take, so `GL_BGRA` is four bytes a texel under
        /// `GL_UNSIGNED_BYTE` and two under `GL_UNSIGNED_SHORT_1_5_5_5_REV`, and naming both BGRA8
        /// created every A1R5G5B5 file at twice its size. A block states its own size, and its data
        /// type means nothing (`sAnyType`). A pair not listed is `Unnamed`.
        struct GlFormat
        {
            GLenum mPixelFormat;
            GLenum mType;
            TextureFormat mColour;
            TextureFormat mData;
        };

        constexpr GLenum sAnyType = 0;

        constexpr GlFormat both(GLenum pixelFormat, GLenum type, TextureFormat format)
        {
            return GlFormat{ pixelFormat, type, format, format };
        }

        constexpr std::array sGlFormats{
            // One format for both spellings: whether the file's header claimed alpha decides
            // nothing, since a BC1 block carries its punch-through bit either way — every mask in
            // the game is a punch-through BC1 block, and almost none of Morrowind's files set
            // `DDPF_ALPHAPIXELS`, so believing the header would leave every canopy a solid card.
            GlFormat{
                GL_COMPRESSED_RGB_S3TC_DXT1_EXT, sAnyType, TextureFormat::Bc1RgbaSrgb, TextureFormat::Bc1RgbaUnorm },
            GlFormat{
                GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, sAnyType, TextureFormat::Bc1RgbaSrgb, TextureFormat::Bc1RgbaUnorm },
            GlFormat{ GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, sAnyType, TextureFormat::Bc2Srgb, TextureFormat::Bc2Unorm },
            GlFormat{ GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, sAnyType, TextureFormat::Bc3Srgb, TextureFormat::Bc3Unorm },
            // One and two channels are no colour: MVR PBR ships three neck diffuse maps as BC5, with
            // their blue gone, and a colour slot refuses them by name rather than drawing them yellow.
            GlFormat{ GL_COMPRESSED_RED_GREEN_RGTC2_EXT, sAnyType, TextureFormat::Unnamed, TextureFormat::Bc5Unorm },
            GlFormat{ GL_COMPRESSED_RED_RGTC1_EXT, sAnyType, TextureFormat::Unnamed, TextureFormat::Bc4Unorm },
            // Not every file the game ships is a block. The sky's cloud decks are plain 32-bit
            // `DDPF_RGB`, which is what a texture painted for a full-screen dome would be, and
            // taking only the compressed formats would draw every weather's clouds grey.
            GlFormat{ GL_RGBA, GL_UNSIGNED_BYTE, TextureFormat::Rgba8Srgb, TextureFormat::Rgba8Unorm },
            GlFormat{ GL_BGRA, GL_UNSIGNED_BYTE, TextureFormat::Bgra8Srgb, TextureFormat::Bgra8Unorm },
            both(GL_RGB, GL_UNSIGNED_BYTE, TextureFormat::Rgb8),
            both(GL_RGB, GL_UNSIGNED_SHORT_5_6_5, TextureFormat::Rgb565),
            both(GL_BGR, GL_UNSIGNED_BYTE, TextureFormat::Bgr8),
            both(GL_LUMINANCE, GL_UNSIGNED_BYTE, TextureFormat::Luminance),
            both(GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, TextureFormat::LuminanceAlpha),
            both(GL_ALPHA, GL_UNSIGNED_BYTE, TextureFormat::Alpha8),
            both(GL_RED, GL_UNSIGNED_BYTE, TextureFormat::Red8),
            both(GL_RG, GL_UNSIGNED_BYTE, TextureFormat::Rg8),
            both(GL_RGBA, GL_UNSIGNED_SHORT, TextureFormat::Rgba16),
            both(GL_LUMINANCE, GL_UNSIGNED_SHORT, TextureFormat::Luminance16),
            both(GL_LUMINANCE_ALPHA, GL_UNSIGNED_SHORT, TextureFormat::LuminanceAlpha16),
            both(GL_RED, GL_UNSIGNED_SHORT, TextureFormat::Red16),
            both(GL_RG, GL_UNSIGNED_SHORT, TextureFormat::Rg16),
            both(GL_RED, GL_HALF_FLOAT, TextureFormat::Red16f),
            both(GL_RG, GL_HALF_FLOAT, TextureFormat::Rg16f),
            both(GL_RGB, GL_HALF_FLOAT, TextureFormat::Rgb16f),
            both(GL_RGBA, GL_HALF_FLOAT, TextureFormat::Rgba16f),
            both(GL_RED, GL_FLOAT, TextureFormat::Red32f),
            both(GL_RG, GL_FLOAT, TextureFormat::Rg32f),
            both(GL_RGB, GL_FLOAT, TextureFormat::Rgb32f),
            both(GL_RGBA, GL_FLOAT, TextureFormat::Rgba32f),
        };
    }

    TextureFormat readFormat(const osg::Image& image, const TextureEncoding encoding)
    {
        const GLenum pixelFormat = image.getPixelFormat();
        const GLenum type = image.getDataType();

        // A sixteen-bit file whose header gave its spare bit no mask: OpenSceneGraph keeps the
        // four-channel pixel format and says so in the internal one.
        if (pixelFormat == GL_BGRA && (type == GL_UNSIGNED_SHORT_1_5_5_5_REV || type == GL_UNSIGNED_SHORT_4_4_4_4_REV))
        {
            const bool opaque = image.getInternalTextureFormat() == GL_RGB;
            if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
                return opaque ? TextureFormat::Xrgb1555 : TextureFormat::Argb1555;
            return opaque ? TextureFormat::Xrgb4444 : TextureFormat::Argb4444;
        }

        for (const GlFormat& row : sGlFormats)
            if (row.mPixelFormat == pixelFormat && (row.mType == sAnyType || row.mType == type))
                return encoding == TextureEncoding::Colour ? row.mColour : row.mData;

        return TextureFormat::Unnamed;
    }

    bool carriesHeight(const osg::Image& normalMap)
    {
        return SceneUtil::computeUnsizedPixelFormat(normalMap.getPixelFormat()) != GL_RG;
    }

    std::string_view nameOf(TextureFormat format)
    {
        return traitsOf(format).mName;
    }
}
