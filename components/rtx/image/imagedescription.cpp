#include "imagedescription.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <osg/Image>
#include <osg/ref_ptr>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/resource/imagemanager.hpp>
#include <components/rtx/common/halffloat.hpp>
#include <components/sceneutil/embeddedimage.hpp>
#include <components/vfs/pathutil.hpp>

#include "colour.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    namespace
    {
        /// The byte nearest what a device samples from a `bits`-bit unsigned normalized channel,
        /// `value / (2^bits - 1)`. Not the bits repeated down the byte, which a BC1 endpoint is
        /// decoded by and which lands a step off that for fourteen of the five- and six-bit values.
        std::byte widenChannel(std::uint32_t value, std::uint32_t bits)
        {
            const std::uint32_t top = (1u << bits) - 1;
            return static_cast<std::byte>((value * 255 + top / 2) / top);
        }

        /// Every texel of `from`, little-endian sixteen-bit words of `format`, into `into` as RGBA8.
        void widenWords(TextureFormat format, std::span<const std::byte> from, std::span<std::byte> into)
        {
            const ChannelBits bits = traitsOf(format).mPacked;
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

        /// One channel of a loose texel as the byte nearest what a device samples from it, held to
        /// nought and one: `LooseTexel` says why. A channel that is no number reads nought.
        std::byte channelByte(const ChannelType type, const std::byte* at)
        {
            const auto unit
                = [](float value) { return unitByte(std::isnan(value) ? 0.0f : std::clamp(value, 0.0f, 1.0f)); };
            const auto word = [&](std::size_t byte) { return std::to_integer<std::uint32_t>(at[byte]) << (8 * byte); };

            switch (type)
            {
                case ChannelType::Unorm8:
                    return at[0];
                case ChannelType::Unorm16:
                    return static_cast<std::byte>(((word(0) | word(1)) * 255 + 32767) / 65535);
                case ChannelType::Half:
                    return unit(fromHalf(static_cast<std::uint16_t>(word(0) | word(1))));
                case ChannelType::Float:
                    return unit(std::bit_cast<float>(word(0) | word(1) | word(2) | word(3)));
            }

            Crash::fatal("a loose channel of no type");
        }

        /// Every texel of `from`, loose channels as `loose` states them, into `into` as RGBA8: each of
        /// the four read from the channel `LooseTexel::mFrom` names, and one the file lacks filled as
        /// a device samples a format that lacks it, nought for a colour and one for alpha.
        void widenLoose(
            const LooseTexel& loose, std::size_t bytes, std::span<const std::byte> from, std::span<std::byte> into)
        {
            const std::size_t channelBytes = bytes / loose.mCount;
            const std::size_t texels = from.size() / bytes;
            assert(into.size() == texels * 4 && "RGBA8 is four bytes a texel");

            for (std::size_t texel = 0; texel < texels; ++texel)
            {
                const std::byte* in = &from[texel * bytes];
                std::byte* out = &into[texel * 4];
                for (std::size_t channel = 0; channel < 4; ++channel)
                {
                    const std::int8_t source = loose.mFrom[channel];
                    out[channel] = source == LooseTexel::sMissing
                        ? (channel == 3 ? std::byte{ 0xFF } : std::byte{ 0 })
                        : channelByte(loose.mType, in + static_cast<std::size_t>(source) * channelBytes);
                }
            }
        }

        /// Every texel of `from`, laid out as `format`, into `into` as RGBA8.
        void widen(TextureFormat format, std::span<const std::byte> from, std::span<std::byte> into)
        {
            const FormatTraits& traits = traitsOf(format);
            switch (traits.mWidening)
            {
                case Widening::Packed:
                    widenWords(format, from, into);
                    return;
                case Widening::Loose:
                    widenLoose(traits.mLoose, traits.mLayout.mBytes, from, into);
                    return;
                case Widening::None:
                    break;
            }

            Crash::fatal("a format widened that this does not widen");
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
            // A format of data alone that a colour slot read: named, since the file is a known
            // format in the wrong slot, and not one this renderer cannot upload.
            if (format == TextureFormat::Unnamed)
                if (const TextureFormat data = readFormat(image, TextureEncoding::Data); data != TextureFormat::Unnamed)
                    return Err{ "its format is " + std::string(nameOf(data))
                        + ", which holds no colour, where a colour is read" };

            if (!isUploadable(format) && !isWidened(format))
                return Err{ "its format is " + std::string(nameOf(format)) + " ("
                    + std::to_string(image.getPixelFormat()) + "), which this renderer does not upload" };

            if (!image.valid() || image.s() < 0 || image.t() < 0)
                return Err{ "it is " + std::to_string(image.s()) + " by " + std::to_string(image.t())
                    + " texels, which no device holds" };

            return {};
        }
    }

    std::uint32_t levelCountOf(const osg::Image& image)
    {
        return std::min(image.getNumMipmapLevels(),
            levelsTo1x1(static_cast<std::uint32_t>(image.s()), static_cast<std::uint32_t>(image.t())));
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
        // Mid grey and not magenta, because a live graph's unreadable textures are mostly things that were never files.
        // Only a base colour draws it; every other reader reads the slot as none, `TEXTURE_STANDS_IN`. One opaque BC1
        // block, both endpoints 0x8410: RGB565 (16, 16, 16), a touch above half, which is mid grey once the sRGB curve
        // is undone.
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

    namespace
    {
        /// What a level `across` by `down` of `format` takes once laid into the caller's texels:
        /// four bytes a texel where the format is one this widens, its own size otherwise.
        std::size_t laidLevelBytes(const TextureFormat format, const std::uint32_t across, const std::uint32_t down)
        {
            return isWidened(format) ? std::size_t{ across } * down * 4 : layoutOf(format).levelBytes(across, down);
        }

        /// `laidBytes` for the first `count` levels, which is what `describeLevels` lays.
        std::size_t laidBytesOf(const osg::Image& image, const TextureFormat format, const std::uint32_t count)
        {
            if (!isWidened(format) && (!isUploadable(format) || slicesAdjoin(image, count)))
                return 0;
            if (image.s() <= 0 || image.t() <= 0)
                return 0;

            const auto width = static_cast<std::uint32_t>(image.s());
            const auto height = static_cast<std::uint32_t>(image.t());

            std::size_t bytes = 0;
            for (std::uint32_t level = 0; level < count; ++level)
                bytes += laidLevelBytes(format, std::max(width >> level, 1u), std::max(height >> level, 1u));

            return bytes;
        }
    }

    std::size_t laidBytes(const osg::Image& image, const TextureFormat format)
    {
        return laidBytesOf(image, format, levelCountOf(image));
    }

    Result<TextureData, std::string> describeImage(const osg::Image& image, const TextureEncoding encoding,
        std::vector<MipLevel>& levels, std::vector<std::byte>& texels)
    {
        return describeImage(image, readFormat(image, encoding), encoding, levels, texels);
    }

    namespace
    {
        /// `describeImage` of the first `count` levels, which is no more than the image keeps. What
        /// is laid out is appended to `texels`.
        Result<TextureData, std::string> describeLevels(const osg::Image& image, const TextureFormat format,
            const TextureEncoding encoding, const std::uint32_t count, std::vector<MipLevel>& levels,
            std::vector<std::byte>& texels)
        {
            // Every reader of an image's bytes on the processor comes through here first, so a crash
            // in one names the file.
            const std::string_view name = SceneUtil::EmbeddedImage::nameOf(image);
            const Crash::NoteScope noted("describing the texture \"{}\"", name);

            if (const Result<void, std::string> uploadable = checkFormat(image, format); !uploadable.isOk())
                return Err{ uploadable.error() };

            const TexelLayout layout = layoutOf(format);
            const auto width = static_cast<std::uint32_t>(image.s());
            const auto height = static_cast<std::uint32_t>(image.t());

            // **The layout held against OpenSceneGraph's before a byte is read.** Every reader and the upload take a
            // level's size from `layout`, and the image's buffer is what its loader allocated level by level
            // (`computeImageSizeInBytes`); where the two differ, reading by `layout` runs past the buffer or short of
            // its levels. Level by level and not by `getTotalSizeInBytes`, whose count of a block format with no chain
            // differs between OpenSceneGraph builds. The bytes are the kept levels' and no more, so an upload stages
            // none of what they leave out.
            //
            // **A volume is its first slice**, as the rasterizer draws a file bound as a flat texture: each level is
            // read at its own offset for one slice, and every slice counts toward where the next level begins. A 128 by
            // 128 DXT3 file of four slices once crashed the staging copy by being staged at all four.
            //
            // **A level's offset is 32 bits**, which neither the bound on what is read nor the copy holds: RGBA8 of
            // 32768 on a side lays four gigabytes at its finest, and widening multiplies an eight-bit source by four.
            std::size_t laidTotal = 0;
            for (std::uint32_t level = 0; level < count; ++level)
                laidTotal += laidLevelBytes(format, std::max(width >> level, 1u), std::max(height >> level, 1u));
            if (laidTotal > std::numeric_limits<std::uint32_t>::max())
                return Err{ "its levels laid out are " + std::to_string(laidTotal)
                    + " bytes, past what a level's 32-bit offset reaches" };

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
                .mName = name,
            };
            const bool widened = isWidened(format);
            if (!widened && slicesAdjoin(image, count))
                return described;

            // **Laid into `texels`, which the caller holds**, so the description spans storage that
            // outlives this call as the image's own does: level by level from where each lies in the
            // image, widened to RGBA8 where the format is one this widens. Every level begins where
            // the texels before it end, and the total is the one a caller reserved by.
            const std::size_t total = laidBytesOf(image, format, count);

            const std::size_t from = texels.size();
            texels.reserve(from + total);
            texels.resize(from + total);
            const std::span<std::byte> into = std::span(texels).subspan(from);
            std::size_t at = 0;
            for (std::uint32_t level = 0; level < count; ++level)
            {
                MipLevel& laidOut = levels[first + level];
                const std::size_t bytes = layout.levelBytes(laidOut.mWidth, laidOut.mHeight);
                const std::span<const std::byte> source(data + image.getMipmapOffset(level), bytes);
                const std::span<std::byte> target
                    = into.subspan(at, laidLevelBytes(format, laidOut.mWidth, laidOut.mHeight));
                if (widened)
                    widen(format, source, target);
                else
                    std::copy(source.begin(), source.end(), target.begin());

                laidOut.mOffset = static_cast<std::uint32_t>(at);
                at += target.size();
            }

            if (widened)
                described.mFormat = isColour(encoding) ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
            described.mBytes = into;
            return described;
        }
    }

    Result<TextureData, std::string> describeImage(const osg::Image& image, const TextureFormat format,
        const TextureEncoding encoding, std::vector<MipLevel>& levels, std::vector<std::byte>& texels)
    {
        return describeLevels(image, format, encoding, levelCountOf(image), levels, texels);
    }

    Result<TextureData, std::string> describeFinestLevel(
        const osg::Image& image, std::vector<MipLevel>& levels, std::vector<std::byte>& texels)
    {
        const TextureEncoding encoding = TextureEncoding::Colour;
        return describeLevels(image, readFormat(image, encoding), encoding, 1, levels, texels);
    }
}
