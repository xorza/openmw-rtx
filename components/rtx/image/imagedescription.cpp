#include "imagedescription.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/vfs/pathutil.hpp>

#include "texels.hpp"

namespace Rtx
{
    namespace
    {
        /// How many of an image's levels a description keeps: as many as the file carries, and no
        /// more than reach a single texel, because a header may count levels past the last one and
        /// a device takes no image with more levels than its size has.
        std::uint32_t keptLevels(const osg::Image& image)
        {
            return std::min(image.getNumMipmapLevels(),
                levelsTo1x1(static_cast<std::uint32_t>(image.s()), static_cast<std::uint32_t>(image.t())));
        }

        /// How many bits each channel of a sixteen-bit format takes, from the high bit down: alpha,
        /// red, green and blue. Nought alpha bits is an opaque texel.
        struct ChannelBits
        {
            std::uint32_t mAlpha = 0;
            std::uint32_t mRed = 0;
            std::uint32_t mGreen = 0;
            std::uint32_t mBlue = 0;
        };

        ChannelBits channelBitsOf(TextureFormat format)
        {
            switch (format)
            {
                case TextureFormat::Rgb565:
                    return ChannelBits{ .mRed = 5, .mGreen = 6, .mBlue = 5 };
                case TextureFormat::Argb1555:
                    return ChannelBits{ .mAlpha = 1, .mRed = 5, .mGreen = 5, .mBlue = 5 };
                case TextureFormat::Xrgb1555:
                    return ChannelBits{ .mRed = 5, .mGreen = 5, .mBlue = 5 };
                case TextureFormat::Argb4444:
                    return ChannelBits{ .mAlpha = 4, .mRed = 4, .mGreen = 4, .mBlue = 4 };
                case TextureFormat::Xrgb4444:
                    return ChannelBits{ .mRed = 4, .mGreen = 4, .mBlue = 4 };
                default:
                    break;
            }

            Crash::fatal("a format widened that is not sixteen bits a texel");
        }

        /// The byte nearest what a device samples from a `bits`-bit unsigned normalized channel,
        /// `value / (2^bits - 1)`. Not the bits repeated down the byte, which a BC1 endpoint is
        /// decoded by and which lands a step off that for fourteen of the five- and six-bit values.
        std::byte widenChannel(std::uint32_t value, std::uint32_t bits)
        {
            const std::uint32_t top = (1u << bits) - 1;
            return static_cast<std::byte>((value * 255 + top / 2) / top);
        }

        /// Every texel of `from`, little-endian sixteen-bit words of `format`, into `into` as RGBA8.
        void widen(TextureFormat format, std::span<const std::byte> from, std::span<std::byte> into)
        {
            const ChannelBits bits = channelBitsOf(format);
            const std::uint32_t greenAt = bits.mBlue;
            const std::uint32_t redAt = greenAt + bits.mGreen;
            const std::uint32_t alphaAt = redAt + bits.mRed;
            const auto field = [](std::uint32_t word, std::uint32_t at, std::uint32_t width) {
                return (word >> at) & ((1u << width) - 1);
            };

            assert(into.size() == from.size() * 2 && "RGBA8 is twice a sixteen-bit texel");
            for (std::size_t texel = 0; texel < from.size() / 2; ++texel)
            {
                const std::uint32_t word = std::to_integer<std::uint32_t>(from[texel * 2])
                    | std::to_integer<std::uint32_t>(from[texel * 2 + 1]) << 8;
                std::byte* out = &into[texel * 4];
                out[0] = widenChannel(field(word, redAt, bits.mRed), bits.mRed);
                out[1] = widenChannel(field(word, greenAt, bits.mGreen), bits.mGreen);
                out[2] = widenChannel(field(word, 0, bits.mBlue), bits.mBlue);
                out[3] = bits.mAlpha > 0 ? widenChannel(field(word, alphaAt, bits.mAlpha), bits.mAlpha)
                                         : std::byte{ 0xFF };
            }
        }

        /// Whether the first slices of an image's first `levels` lie back to back, which is what a
        /// description spans. A volume's levels are each every slice of it, so from its second
        /// level on they do not.
        bool slicesAdjoin(const osg::Image& image, const std::uint32_t levels)
        {
            return image.r() == 1 || levels == 1;
        }

        /// `checkUploadable` of a format already read.
        Result<void, std::string> checkFormat(const osg::Image& image, const TextureFormat format)
        {
            if (!isUploadable(format) && !isWidened(format))
                return Err{ "its format is " + std::string(nameOf(format)) + " ("
                    + std::to_string(image.getPixelFormat()) + "), which this renderer does not upload" };

            if (!image.valid() || image.s() < 0 || image.t() < 0)
                return Err{ "it is " + std::to_string(image.s()) + " by " + std::to_string(image.t())
                    + " texels, which no device holds" };

            return {};
        }
    }

    Result<osg::ref_ptr<const osg::Image>, std::string> openImage(
        Resource::ImageManager& images, const VFS::Path::NormalizedView path)
    {
        osg::ref_ptr<const osg::Image> image;
        try
        {
            image = images.getImage(path);
        }
        catch (const std::exception& failed)
        {
            return Err{ std::string(failed.what()) };
        }

        // The manager answers a file it cannot read with its warning image, having logged why:
        // an image, and not the one the file holds.
        if (image == nullptr || image == images.getWarningImage())
            return Err{ std::string(sNoImage) };

        return image;
    }

    TextureData describeStandIn()
    {
        // Mid grey and not magenta, because a live graph's unreadable textures are mostly things
        // that were never files, and the refusal already names each. Only a base colour draws it;
        // every other reader reads the slot as none, `TEXTURE_STANDS_IN`. One opaque BC1 block with both
        // endpoints the same grey: 0x8410 is RGB565 for (16, 16, 16) out of (31, 63, 31) — a touch
        // above half, which is mid grey once the sRGB curve is undone.
        static constexpr std::array<std::byte, 8> sBlock{ std::byte{ 0x10 }, std::byte{ 0x84 }, std::byte{ 0x10 },
            std::byte{ 0x84 }, std::byte{}, std::byte{}, std::byte{}, std::byte{} };
        static constexpr MipLevel sLevel{ .mOffset = 0, .mWidth = 4, .mHeight = 4 };

        return TextureData{
            .mSource = TextureSource::StandIn,
            .mFormat = TextureFormat::Bc1RgbaSrgb,
            .mWidth = 4,
            .mHeight = 4,
            .mBytes = sBlock,
            .mLevels = std::span<const MipLevel>(&sLevel, 1),
            .mName = "stand-in",
        };
    }

    Result<void, std::string> checkUploadable(const osg::Image& image, const TextureEncoding encoding)
    {
        return checkFormat(image, readFormat(image, encoding));
    }

    std::size_t laidBytes(const osg::Image& image, const TextureFormat format)
    {
        const bool widened = isWidened(format);
        if (!widened && (!isUploadable(format) || slicesAdjoin(image, keptLevels(image))))
            return 0;
        if (image.s() <= 0 || image.t() <= 0)
            return 0;

        const auto width = static_cast<std::uint32_t>(image.s());
        const auto height = static_cast<std::uint32_t>(image.t());
        const TexelLayout laid = layoutOf(widened ? TextureFormat::Rgba8Unorm : format);

        std::size_t bytes = 0;
        for (std::uint32_t level = 0; level < keptLevels(image); ++level)
            bytes += laid.levelBytes(std::max(width >> level, 1u), std::max(height >> level, 1u));

        return bytes;
    }

    Result<TextureData, std::string> describeImage(const osg::Image& image, std::vector<MipLevel>& levels,
        std::vector<std::byte>& texels, const TextureEncoding encoding)
    {
        return describeImage(image, readFormat(image, encoding), encoding, levels, texels);
    }

    namespace
    {
        /// `describeImage` of the first `count` levels, which is no more than the image keeps.
        Result<TextureData, std::string> describeLevels(const osg::Image& image, const TextureFormat format,
            const TextureEncoding encoding, const std::uint32_t count, std::vector<MipLevel>& levels,
            std::vector<std::byte>& texels)
        {
            // Every reader of an image's bytes on the processor comes through here first, so a crash
            // in one names the file.
            const Crash::NoteScope noted("describing the texture \"{}\"", image.getFileName());

            if (const Result<void, std::string> uploadable = checkFormat(image, format); !uploadable.isOk())
                return Err{ uploadable.error() };

            const TexelLayout layout = layoutOf(format);
            const auto width = static_cast<std::uint32_t>(image.s());
            const auto height = static_cast<std::uint32_t>(image.t());

            // **The layout held against OpenSceneGraph's before a byte is read.** Every reader of the
            // bytes below and the upload take a level's size from `layout`, and the image's buffer is
            // what its loader allocated level by level, `computeImageSizeInBytes`; where the two count
            // a level differently, reading the image by `layout` runs past its buffer or short of its
            // levels. A format read by its pixel format alone did that to every sixteen-bit file — and
            // a padded row would do it again. Level by level and not by `getTotalSizeInBytes`, whose
            // count of a block format with no chain differs between OpenSceneGraph builds: a two-by-two
            // BC3 is four bytes of its sixteen in one and 32 in another. The bytes are the kept levels'
            // and no more, so an upload stages none of what they leave out.
            //
            // **A volume is its first slice**, as the rasterizer draws a file bound as a flat texture:
            // each level is read at its own offset for one slice, and every slice counts toward where
            // the next level begins. A player's crash in the staging copy was a 128 by 128 DXT3 file of
            // four slices staged at all four.
            const std::size_t first = levels.size();
            const auto depth = static_cast<std::uint32_t>(image.r());
            std::size_t kept = 0;
            std::size_t laid = 0;
            for (std::uint32_t level = 0; level < count; ++level)
            {
                const std::uint32_t across = std::max(width >> level, 1u);
                const std::uint32_t down = std::max(height >> level, 1u);
                const std::size_t ours = layout.levelBytes(across, down);
                const auto theirs = [&](std::uint32_t slices) -> std::size_t {
                    return osg::Image::computeImageSizeInBytes(static_cast<int>(across), static_cast<int>(down),
                        static_cast<int>(slices), image.getPixelFormat(), image.getDataType(), image.getPacking());
                };

                if (image.getMipmapOffset(level) != laid || ours != theirs(1))
                {
                    levels.resize(first);
                    return Err{ "its level " + std::to_string(level) + " is " + std::to_string(theirs(1))
                        + " bytes at byte " + std::to_string(image.getMipmapOffset(level)) + ", where "
                        + std::string(nameOf(format)) + " at " + std::to_string(across) + " by " + std::to_string(down)
                        + " is " + std::to_string(ours) + " at byte " + std::to_string(laid) };
                }

                levels.push_back(
                    MipLevel{ .mOffset = static_cast<std::uint32_t>(kept), .mWidth = across, .mHeight = down });
                kept += ours;
                laid += theirs(std::max(depth >> level, 1u));
            }

            const auto* const data = reinterpret_cast<const std::byte*>(image.data());
            TextureData described{
                .mFormat = format,
                .mEncoding = encoding,
                .mWidth = width,
                .mHeight = height,
                .mBytes = std::span(data, kept),
                .mLevels = std::span<const MipLevel>(levels).subspan(first, count),
                .mName = image.getFileName(),
            };
            const bool widened = isWidened(format);
            if (!widened && slicesAdjoin(image, count))
                return described;

            // **Laid into `texels`, which the caller holds**, so the description spans storage that
            // outlives this call as the image's own does: level by level from where each lies in the
            // image, widened where the format is sixteen bits a texel. Widened is twice the bytes,
            // because RGBA8 is four bytes a texel, and so every level begins at twice the offset.
            const std::size_t scale = widened ? 2 : 1;
            const std::size_t from = texels.size();
            texels.resize(from + kept * scale);
            const std::span<std::byte> into = std::span(texels).subspan(from);
            for (std::uint32_t level = 0; level < count; ++level)
            {
                MipLevel& laidOut = levels[first + level];
                const std::size_t bytes = layout.levelBytes(laidOut.mWidth, laidOut.mHeight);
                const std::span<const std::byte> source(data + image.getMipmapOffset(level), bytes);
                const std::span<std::byte> target = into.subspan(laidOut.mOffset * scale, bytes * scale);
                if (widened)
                    widen(format, source, target);
                else
                    std::copy(source.begin(), source.end(), target.begin());

                laidOut.mOffset *= static_cast<std::uint32_t>(scale);
            }

            if (widened)
                described.mFormat
                    = encoding == TextureEncoding::Colour ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
            described.mBytes = std::span<const std::byte>(texels).subspan(from, kept * scale);
            return described;
        }
    }

    Result<TextureData, std::string> describeImage(const osg::Image& image, const TextureFormat format,
        const TextureEncoding encoding, std::vector<MipLevel>& levels, std::vector<std::byte>& texels)
    {
        return describeLevels(image, format, encoding, keptLevels(image), levels, texels);
    }

    Result<TextureData, std::string> describeFinestLevel(
        const osg::Image& image, std::vector<MipLevel>& levels, std::vector<std::byte>& texels)
    {
        const TextureEncoding encoding = TextureEncoding::Colour;
        return describeLevels(image, readFormat(image, encoding), encoding, 1, levels, texels);
    }
}
