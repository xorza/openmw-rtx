#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <components/crashcatcher/crash.hpp>

#include "textureencoding.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// Every format OpenSceneGraph decodes a texture into: the ones this renderer uploads first,
    /// then the ones it widens to one of those on the way in, then the ones it only counts, because
    /// a file the uploader refuses is still a file the report has to name. A colour's formats are
    /// sRGB, because the files hold display-encoded bytes and the hardware converts inside the
    /// filter; data's are the same blocks read linearly (`TextureEncoding`). `readFormat` names one
    /// by the image's pixel format and its data type together, because the pixel format alone says
    /// which channels and not how many bytes they take. What each one is, is its row of
    /// `sFormatTraits`.
    enum class TextureFormat : std::uint8_t
    {
        /// BC1 with its punch-through alpha bit read. Both DXT1 spellings land here, and
        /// `readFormat`'s table says why the header's alpha flag is not consulted.
        Bc1RgbaSrgb,
        Bc2Srgb,
        Bc3Srgb,

        /// Uncompressed, and not sRGB: a data map stored loose, and what a test asserting an exact
        /// texel needs, because a block cannot express an arbitrary value and an sRGB format would
        /// land the assertion on the far side of a transfer function.
        Rgba8Unorm,

        /// Uncompressed and display-encoded, in the two channel orders a `.dds` states them in. The
        /// cloud decks are 32-bit `DDPF_RGB`, and a renderer that takes only blocks draws every
        /// weather grey. Both orders rather than one and a swizzle, because converting would mean
        /// owning a copy of a buffer this type is defined by not owning.
        Rgba8Srgb,
        Bgra8Srgb,

        /// The same blocks and orders as data. A companion map is BC1 or BC3 nearly always.
        Bc1RgbaUnorm,
        Bc2Unorm,
        Bc3Unorm,
        Bgra8Unorm,

        /// Two channels, as OpenMW takes a normal map with its Z left out, and one, as ATI1 holds
        /// a single map. Data only: a file of one or two channels bound as a colour has lost its
        /// blue, and is refused as a colour, where GL would draw it red or yellow.
        Bc5Unorm,
        Bc4Unorm,

        /// What a backend encodes a texture it makes into, display-encoded and as data: the ground's
        /// composites. Never a file's, since no reader names one.
        Bc7Srgb,
        Bc7Unorm,

        /// Sixteen bits a texel, as the old mods' `.dds` files hold them, in the channel order the
        /// file states from the high bit down: `describeImage` widens each to RGBA8 in the encoding
        /// the slot asks for, because a device's sixteen-bit formats have no sRGB spelling. The `X`
        /// ones carry a bit the file leaves undefined, and are opaque.
        Rgb565,
        Argb1555,
        Xrgb1555,
        Argb4444,
        Xrgb4444,

        /// Loose channels that are not four bytes a texel, as an old mod's 24-bit `.tga` or `.bmp`,
        /// a grey or alpha-only `.dds` and a DX10 file of one, two, sixteen-bit or float channels hold
        /// them: `describeImage` widens each to RGBA8 too, every channel the file lacks filled as GL
        /// samples it — nought for a colour, one for alpha, and a luminance read in all three
        /// colours (`LooseTexel`). A float or sixteen-bit channel is rounded to a byte and held to
        /// nought and one: a colour slot is display-encoded and a data map a fraction, so a value
        /// past either end has nothing to mean here.
        Rgb8,
        Bgr8,

        /// Thirty-two bits a texel whose header gave the fourth byte no mask, which the file leaves
        /// undefined and many tools write as nought: opaque, as GL samples the `GL_RGB` OpenSceneGraph
        /// states them in, and never a hole cut by whatever the spare byte holds.
        Xbgr8,
        Xrgb8,

        Luminance,
        LuminanceAlpha,
        Alpha8,
        Red8,
        Rg8,
        Rgba16,
        Luminance16,
        LuminanceAlpha16,
        Red16,
        Rg16,
        Red16f,
        Rg16f,
        Rgb16f,
        Rgba16f,
        Red32f,
        Rg32f,
        Rgb32f,
        Rgba32f,

        /// Anything else, and there is one count of them rather than one each.
        Unnamed,
    };

    inline constexpr std::size_t sTextureFormatCount = static_cast<std::size_t>(TextureFormat::Unnamed) + 1;

    /// How a format lays its texels out: square blocks `mSide` texels across of `mBytes` bytes
    /// each, rows tight. A loose format is a block of one texel.
    ///
    /// **The one statement of how many bytes a texel takes**, which the level arithmetic, the
    /// upload and every reader of a description's bytes go through. `describeImage` holds it
    /// against what OpenSceneGraph says of the same image, so a format whose bytes the two count
    /// differently is refused by name before any of them is read.
    struct TexelLayout
    {
        std::uint32_t mSide = 1;
        std::uint32_t mBytes = 0;

        bool isBlocked() const { return mSide > 1; }

        /// How many bytes a level `width` by `height` texels takes.
        std::size_t levelBytes(std::uint32_t width, std::uint32_t height) const
        {
            return std::size_t{ (width + mSide - 1) / mSide } * ((height + mSide - 1) / mSide) * mBytes;
        }
    };

    /// How `describeImage` lays a format on the way in.
    enum class Widening : std::uint8_t
    {
        /// Not at all: the backend takes it as it is.
        None,

        /// One little-endian sixteen-bit word a texel, `ChannelBits` wide each.
        Packed,

        /// Channels of one type side by side, `LooseTexel`.
        Loose,
    };

    /// How many bits each channel of a sixteen-bit format takes, from the high bit down: alpha,
    /// red, green and blue. Nought alpha bits is an opaque texel.
    struct ChannelBits
    {
        std::uint8_t mAlpha = 0;
        std::uint8_t mRed = 0;
        std::uint8_t mGreen = 0;
        std::uint8_t mBlue = 0;
    };

    /// What one channel of a loose texel is stored as.
    enum class ChannelType : std::uint8_t
    {
        Unorm8,
        Unorm16,
        Half,
        Float,
    };

    /// A loose texel of `mCount` channels of `mType`, and which of them each of red, green, blue
    /// and alpha reads: an index into the texel, or `sMissing` for a channel the file lacks, which
    /// GL samples as nought for a colour and one for alpha.
    struct LooseTexel
    {
        static constexpr std::int8_t sMissing = -1;

        ChannelType mType = ChannelType::Unorm8;
        std::uint8_t mCount = 0;
        std::array<std::int8_t, 4> mFrom{ sMissing, sMissing, sMissing, sMissing };
    };

    /// What a format is, one row a format, in the enum's order: the one statement of its name, its
    /// layout, its encoding and how it arrives. A format added to the enum without a row is a
    /// build failure (`sFormatTraitsInOrder`).
    struct FormatTraits
    {
        TextureFormat mFormat;
        std::string_view mName;
        TexelLayout mLayout{};

        /// Display-encoded bytes, which every colour format holds. A data format's are not, and
        /// neither are `Rgba8Unorm`'s, which a test also uses: a value written into it is the value
        /// light transport sees, with no transfer function between the expectation and the answer.
        bool mSrgb = false;

        /// Colours stated blue first, which every reader of its bytes has to know to read red as red.
        bool mBgr = false;

        /// Whether the host reads a colour out of it: an RGBA8 texel or a BC1, BC2 or BC3 block. Not
        /// BC4 or BC5, whose blocks hold one or two channels of data.
        bool mColour = false;

        Widening mWidening = Widening::None;
        ChannelBits mPacked{};
        LooseTexel mLoose{};
    };

    namespace FormatRows
    {
        constexpr FormatTraits block(
            TextureFormat format, std::string_view name, std::uint32_t bytes, bool srgb, bool colour)
        {
            return FormatTraits{ .mFormat = format,
                .mName = name,
                .mLayout = TexelLayout{ .mSide = 4, .mBytes = bytes },
                .mSrgb = srgb,
                .mColour = colour };
        }

        constexpr FormatTraits rgba8(TextureFormat format, std::string_view name, bool srgb, bool bgr)
        {
            return FormatTraits{ .mFormat = format,
                .mName = name,
                .mLayout = TexelLayout{ .mBytes = 4 },
                .mSrgb = srgb,
                .mBgr = bgr,
                .mColour = true };
        }

        constexpr FormatTraits packed(TextureFormat format, std::string_view name, ChannelBits bits)
        {
            return FormatTraits{ .mFormat = format,
                .mName = name,
                .mLayout = TexelLayout{ .mBytes = 2 },
                .mWidening = Widening::Packed,
                .mPacked = bits };
        }

        constexpr std::uint32_t channelBytes(ChannelType type)
        {
            switch (type)
            {
                case ChannelType::Unorm8:
                    return 1;
                case ChannelType::Unorm16:
                case ChannelType::Half:
                    return 2;
                case ChannelType::Float:
                    return 4;
            }
            return 0;
        }

        /// `spare` channels past the last one read: a texel the file pads.
        constexpr FormatTraits loose(TextureFormat format, std::string_view name, ChannelType type,
            std::array<std::int8_t, 4> from, std::uint8_t spare = 0)
        {
            std::uint8_t count = 0;
            for (const std::int8_t channel : from)
                count = std::max<std::uint8_t>(count, static_cast<std::uint8_t>(channel + 1));
            count = static_cast<std::uint8_t>(count + spare);
            return FormatTraits{ .mFormat = format,
                .mName = name,
                .mLayout = TexelLayout{ .mBytes = channelBytes(type) * count },
                .mWidening = Widening::Loose,
                .mLoose = LooseTexel{ .mType = type, .mCount = count, .mFrom = from } };
        }

        constexpr std::int8_t sNo = LooseTexel::sMissing;
        constexpr std::array<std::int8_t, 4> sRgba{ 0, 1, 2, 3 };
        constexpr std::array<std::int8_t, 4> sRgb{ 0, 1, 2, sNo };
        constexpr std::array<std::int8_t, 4> sRg{ 0, 1, sNo, sNo };
        constexpr std::array<std::int8_t, 4> sRed{ 0, sNo, sNo, sNo };
    }

    inline constexpr std::array<FormatTraits, sTextureFormatCount> sFormatTraits{ {
        FormatRows::block(TextureFormat::Bc1RgbaSrgb, "BC1 (DXT1)", 8, true, true),
        FormatRows::block(TextureFormat::Bc2Srgb, "BC2 (DXT3)", 16, true, true),
        FormatRows::block(TextureFormat::Bc3Srgb, "BC3 (DXT5)", 16, true, true),
        FormatRows::rgba8(TextureFormat::Rgba8Unorm, "RGBA8 (linear)", false, false),
        FormatRows::rgba8(TextureFormat::Rgba8Srgb, "RGBA8", true, false),
        FormatRows::rgba8(TextureFormat::Bgra8Srgb, "BGRA8", true, true),
        FormatRows::block(TextureFormat::Bc1RgbaUnorm, "BC1 (DXT1, linear)", 8, false, true),
        FormatRows::block(TextureFormat::Bc2Unorm, "BC2 (DXT3, linear)", 16, false, true),
        FormatRows::block(TextureFormat::Bc3Unorm, "BC3 (DXT5, linear)", 16, false, true),
        FormatRows::rgba8(TextureFormat::Bgra8Unorm, "BGRA8 (linear)", false, true),
        FormatRows::block(TextureFormat::Bc5Unorm, "BC5 (ATI2, linear)", 16, false, false),
        FormatRows::block(TextureFormat::Bc4Unorm, "BC4 (ATI1, linear)", 8, false, false),
        FormatRows::block(TextureFormat::Bc7Srgb, "BC7", 16, true, true),
        FormatRows::block(TextureFormat::Bc7Unorm, "BC7 (linear)", 16, false, true),
        FormatRows::packed(TextureFormat::Rgb565, "R5G6B5", ChannelBits{ .mRed = 5, .mGreen = 6, .mBlue = 5 }),
        FormatRows::packed(
            TextureFormat::Argb1555, "A1R5G5B5", ChannelBits{ .mAlpha = 1, .mRed = 5, .mGreen = 5, .mBlue = 5 }),
        FormatRows::packed(TextureFormat::Xrgb1555, "X1R5G5B5", ChannelBits{ .mRed = 5, .mGreen = 5, .mBlue = 5 }),
        FormatRows::packed(
            TextureFormat::Argb4444, "A4R4G4B4", ChannelBits{ .mAlpha = 4, .mRed = 4, .mGreen = 4, .mBlue = 4 }),
        FormatRows::packed(TextureFormat::Xrgb4444, "X4R4G4B4", ChannelBits{ .mRed = 4, .mGreen = 4, .mBlue = 4 }),
        FormatRows::loose(TextureFormat::Rgb8, "RGB8", ChannelType::Unorm8, FormatRows::sRgb),
        FormatRows::loose(TextureFormat::Bgr8, "BGR8", ChannelType::Unorm8, { 2, 1, 0, FormatRows::sNo }),
        FormatRows::loose(TextureFormat::Xbgr8, "X8B8G8R8", ChannelType::Unorm8, FormatRows::sRgb, 1),
        FormatRows::loose(TextureFormat::Xrgb8, "X8R8G8B8", ChannelType::Unorm8, { 2, 1, 0, FormatRows::sNo }, 1),
        FormatRows::loose(TextureFormat::Luminance, "L8", ChannelType::Unorm8, { 0, 0, 0, FormatRows::sNo }),
        FormatRows::loose(TextureFormat::LuminanceAlpha, "LA8", ChannelType::Unorm8, { 0, 0, 0, 1 }),
        FormatRows::loose(
            TextureFormat::Alpha8, "A8", ChannelType::Unorm8, { FormatRows::sNo, FormatRows::sNo, FormatRows::sNo, 0 }),
        FormatRows::loose(TextureFormat::Red8, "R8", ChannelType::Unorm8, FormatRows::sRed),
        FormatRows::loose(TextureFormat::Rg8, "R8G8", ChannelType::Unorm8, FormatRows::sRg),
        FormatRows::loose(TextureFormat::Rgba16, "RGBA16", ChannelType::Unorm16, FormatRows::sRgba),
        FormatRows::loose(TextureFormat::Luminance16, "L16", ChannelType::Unorm16, { 0, 0, 0, FormatRows::sNo }),
        FormatRows::loose(TextureFormat::LuminanceAlpha16, "LA16", ChannelType::Unorm16, { 0, 0, 0, 1 }),
        FormatRows::loose(TextureFormat::Red16, "R16", ChannelType::Unorm16, FormatRows::sRed),
        FormatRows::loose(TextureFormat::Rg16, "R16G16", ChannelType::Unorm16, FormatRows::sRg),
        FormatRows::loose(TextureFormat::Red16f, "R16F", ChannelType::Half, FormatRows::sRed),
        FormatRows::loose(TextureFormat::Rg16f, "R16G16F", ChannelType::Half, FormatRows::sRg),
        FormatRows::loose(TextureFormat::Rgb16f, "RGB16F", ChannelType::Half, FormatRows::sRgb),
        FormatRows::loose(TextureFormat::Rgba16f, "RGBA16F", ChannelType::Half, FormatRows::sRgba),
        FormatRows::loose(TextureFormat::Red32f, "R32F", ChannelType::Float, FormatRows::sRed),
        FormatRows::loose(TextureFormat::Rg32f, "R32G32F", ChannelType::Float, FormatRows::sRg),
        FormatRows::loose(TextureFormat::Rgb32f, "RGB32F", ChannelType::Float, FormatRows::sRgb),
        FormatRows::loose(TextureFormat::Rgba32f, "RGBA32F", ChannelType::Float, FormatRows::sRgba),
        FormatTraits{ .mFormat = TextureFormat::Unnamed, .mName = "an unnamed pixel format" },
    } };

    constexpr bool sFormatTraitsInOrder = [] {
        for (std::size_t at = 0; at < sFormatTraits.size(); ++at)
            if (static_cast<std::size_t>(sFormatTraits[at].mFormat) != at)
                return false;
        return true;
    }();
    static_assert(sFormatTraitsInOrder, "a row of sFormatTraits stands where another format's belongs");

    inline const FormatTraits& traitsOf(TextureFormat format)
    {
        return sFormatTraits[static_cast<std::size_t>(format)];
    }

    /// Whether a backend takes a format as it is.
    inline bool isUploadable(const TextureFormat format)
    {
        return format != TextureFormat::Unnamed && traitsOf(format).mWidening == Widening::None;
    }

    /// Whether `describeImage` widens a format to RGBA8 on the way in.
    inline bool isWidened(const TextureFormat format)
    {
        return traitsOf(format).mWidening != Widening::None;
    }

    /// `FormatTraits::mLayout`. `Unnamed` has none: nothing knows it.
    inline TexelLayout layoutOf(TextureFormat format)
    {
        const TexelLayout layout = traitsOf(format).mLayout;
        if (layout.mBytes == 0)
            Crash::fatal("a texture format with no layout");
        return layout;
    }

    /// `FormatTraits::mBgr`.
    inline bool isBgr(TextureFormat format)
    {
        return traitsOf(format).mBgr;
    }

    /// `FormatTraits::mSrgb`.
    inline bool isSrgb(TextureFormat format)
    {
        return traitsOf(format).mSrgb;
    }

    /// Whether a format is BC1, whose blocks carry a punch-through alpha in their endpoint order,
    /// in either encoding.
    inline bool isBc1(TextureFormat format)
    {
        return format == TextureFormat::Bc1RgbaSrgb || format == TextureFormat::Bc1RgbaUnorm;
    }

    /// Which format `image` arrived in, read as `encoding` — the one place a texture's `GLenum`
    /// decides its format, so the uploader and the report cannot disagree. A blend map is weights
    /// and not a texture, and `GroundReader` reads its bytes itself.
    TextureFormat readFormat(const osg::Image& image, TextureEncoding encoding = TextureEncoding::Colour);

    /// Whether a normal map bound for its height has one: not a map of red and green, BC5 or a
    /// loose one, by the rule the rasterizer's `ShaderVisitor` and `Terrain` turn parallax off by
    /// (`SceneUtil::computeUnsizedPixelFormat`). A sampler hands such a map an alpha of one, so the
    /// shift would be the same everywhere.
    bool carriesHeight(const osg::Image& normalMap);

    /// What `format` is called, for a report to print.
    std::string_view nameOf(TextureFormat format);
}
