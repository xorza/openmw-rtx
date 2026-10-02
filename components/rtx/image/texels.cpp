#include "texels.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <osg/GL>
#include <osg/Image>
#include <osg/Texture> // The S3TC and RGTC formats, which Windows's and Apple's gl.h lack.
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
            assert(layout.mBytes == 4 && "a loose texel read as four bytes that is not");
            const std::size_t at = level.mOffset + (std::size_t{ y } * level.mWidth + x) * layout.mBytes;
            const auto channel = [&](std::size_t offset) {
                return std::to_integer<std::uint32_t>(texture.mBytes[at + offset]) / 255.0f;
            };

            // The two loose spellings differ only in which end the three colours are stated from,
            // and a reader that took one order for both draws the sky with its red and blue swapped.
            if (isBgr(texture.mFormat))
                return osg::Vec3f(channel(2), channel(1), channel(0));

            return osg::Vec3f(channel(0), channel(1), channel(2));
        }

        /// The block at `column` and `band`, counted in blocks. Its colour half is the last eight bytes
        /// whichever format it is: BC2 and BC3 put their alpha in front of it and BC1 has none.
        ColourBlock colourBlockAt(const TextureData& texture, const MipLevel& level, const TexelLayout& layout,
            const std::uint32_t column, const std::uint32_t band)
        {
            const std::uint32_t columns = (level.mWidth + 3) / 4;
            const std::size_t at
                = level.mOffset + (std::size_t{ band } * columns + column) * layout.mBytes + (layout.mBytes - 8);
            return ColourBlock::read(texture.mBytes.subspan(at).first<8>(), isBc1(texture.mFormat));
        }
    }

    bool readsColour(const TextureData& texture)
    {
        return !texture.mBytes.empty() && !texture.mLevels.empty() && isUploadable(texture.mFormat)
            && texture.mFormat != TextureFormat::Bc5Unorm;
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

    TextureFormat readFormat(const osg::Image& image, const TextureEncoding encoding)
    {
        const bool colour = encoding == TextureEncoding::Colour;

        // **A loose format is its pixel format and its data type together.** The pixel format says
        // which channels and the data type how many bits they take: `GL_BGRA` is four bytes a texel
        // under `GL_UNSIGNED_BYTE` and two under `GL_UNSIGNED_SHORT_1_5_5_5_REV`, and naming both
        // BGRA8 created every A1R5G5B5 file at twice its size. A pair not listed is `Unnamed`. A
        // block states its own size, and its data type means nothing.
        const GLenum type = image.getDataType();
        const bool bytes = type == GL_UNSIGNED_BYTE;

        // A sixteen-bit file whose header gave its spare bit no mask: OpenSceneGraph keeps the
        // four-channel pixel format and says so in the internal one.
        const bool opaque = image.getInternalTextureFormat() == GL_RGB;

        switch (image.getPixelFormat())
        {
            // One format for both spellings: whether the file's header claimed alpha decides
            // nothing, since a BC1 block carries its punch-through bit either way — every mask in
            // the game is a punch-through BC1 block, and almost none of Morrowind's files set
            // `DDPF_ALPHAPIXELS`, so believing the header would leave every canopy a solid card.
            case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
            case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
                return colour ? TextureFormat::Bc1RgbaSrgb : TextureFormat::Bc1RgbaUnorm;
            case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
                return colour ? TextureFormat::Bc2Srgb : TextureFormat::Bc2Unorm;
            case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
                return colour ? TextureFormat::Bc3Srgb : TextureFormat::Bc3Unorm;
            // Two channels are no colour: MVR PBR ships three neck diffuse maps as BC5, with their
            // blue gone, and a colour slot refuses them by name rather than drawing them yellow.
            case GL_COMPRESSED_RED_GREEN_RGTC2_EXT:
                return colour ? TextureFormat::Unnamed : TextureFormat::Bc5Unorm;
            case GL_RGB:
                return bytes                          ? TextureFormat::Rgb8
                    : type == GL_UNSIGNED_SHORT_5_6_5 ? TextureFormat::Rgb565
                                                      : TextureFormat::Unnamed;
            // Not every file the game ships is a block. The sky's cloud decks are plain 32-bit
            // `DDPF_RGB`, which is what a texture painted for a full-screen dome would be, and
            // taking only the compressed formats would draw every weather's clouds grey.
            case GL_RGBA:
                if (!bytes)
                    return TextureFormat::Unnamed;
                return colour ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
            case GL_BGRA:
                if (type == GL_UNSIGNED_SHORT_1_5_5_5_REV)
                    return opaque ? TextureFormat::Xrgb1555 : TextureFormat::Argb1555;
                if (type == GL_UNSIGNED_SHORT_4_4_4_4_REV)
                    return opaque ? TextureFormat::Xrgb4444 : TextureFormat::Argb4444;
                if (!bytes)
                    return TextureFormat::Unnamed;
                return colour ? TextureFormat::Bgra8Srgb : TextureFormat::Bgra8Unorm;
            case GL_BGR:
                return bytes ? TextureFormat::Bgr8 : TextureFormat::Unnamed;
            case GL_LUMINANCE:
                return bytes ? TextureFormat::Luminance : TextureFormat::Unnamed;
            case GL_LUMINANCE_ALPHA:
                return bytes ? TextureFormat::LuminanceAlpha : TextureFormat::Unnamed;
            default:
                return TextureFormat::Unnamed;
        }
    }

    bool carriesHeight(const osg::Image& normalMap)
    {
        return readFormat(normalMap, TextureEncoding::Data) != TextureFormat::Bc5Unorm;
    }

    std::string_view nameOf(TextureFormat format)
    {
        switch (format)
        {
            case TextureFormat::Bc1RgbaSrgb:
                return "BC1 (DXT1)";
            case TextureFormat::Bc2Srgb:
                return "BC2 (DXT3)";
            case TextureFormat::Bc3Srgb:
                return "BC3 (DXT5)";
            case TextureFormat::Rgba8Unorm:
                return "RGBA8 (linear)";
            case TextureFormat::Rgb8:
                return "RGB8";
            case TextureFormat::Bgr8:
                return "BGR8";
            case TextureFormat::Rgba8Srgb:
                return "RGBA8";
            case TextureFormat::Bgra8Srgb:
                return "BGRA8";
            case TextureFormat::Bc1RgbaUnorm:
                return "BC1 (DXT1, linear)";
            case TextureFormat::Bc2Unorm:
                return "BC2 (DXT3, linear)";
            case TextureFormat::Bc3Unorm:
                return "BC3 (DXT5, linear)";
            case TextureFormat::Bgra8Unorm:
                return "BGRA8 (linear)";
            case TextureFormat::Bc5Unorm:
                return "BC5 (ATI2, linear)";
            case TextureFormat::Rgb565:
                return "R5G6B5";
            case TextureFormat::Argb1555:
                return "A1R5G5B5";
            case TextureFormat::Xrgb1555:
                return "X1R5G5B5";
            case TextureFormat::Argb4444:
                return "A4R4G4B4";
            case TextureFormat::Xrgb4444:
                return "X4R4G4B4";
            case TextureFormat::Luminance:
                return "L8";
            case TextureFormat::LuminanceAlpha:
                return "LA8";
            case TextureFormat::Unnamed:
                break;
        }

        return "an unnamed pixel format";
    }
}
