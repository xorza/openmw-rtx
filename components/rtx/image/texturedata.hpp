#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/common/index.hpp>

#include "textureencoding.hpp"
#include "texturewrap.hpp"

namespace Rtx
{
    /// Where one mip level sits in a texture's bytes, and how big it is.
    struct MipLevel
    {
        std::uint32_t mOffset = 0;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// Where texel `x`, `y` of a loose level begins, at `bytes` a texel.
        constexpr std::size_t texelOffset(std::uint32_t x, std::uint32_t y, std::size_t bytes) const
        {
            return mOffset + (std::size_t{ y } * mWidth + x) * bytes;
        }

        /// Where the block at `column` and `band` of a block-compressed level begins, counted in
        /// blocks of four texels a side from the level's corner, at `bytes` a block.
        constexpr std::size_t blockOffset(std::uint32_t column, std::uint32_t band, std::size_t bytes) const
        {
            return mOffset + (std::size_t{ band } * ((mWidth + 3) / 4) + column) * bytes;
        }
    };

    /// How many levels a chain from `width` by `height` down to one texel has: what
    /// `MipPyramid::layOutTo1x1` lays out, for an image made to hold one.
    inline std::uint32_t levelsTo1x1(std::uint32_t width, std::uint32_t height)
    {
        return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
    }

    struct TexelLayout;

    /// The shape of a chain of mip levels: where each one sits and how big it is. The shape and not
    /// the texels, because four payloads build the same chain.
    struct MipPyramid
    {
        std::vector<MipLevel> mLevels;

        void reuse() { mLevels.clear(); }

        bool isEmpty() const { return mLevels.empty(); }

        std::uint32_t getLevelCount() const { return static_cast<std::uint32_t>(mLevels.size()); }

        const MipLevel& getLevel(std::uint32_t level) const
        {
            assert(level < mLevels.size() && "a level past the end of the chain");
            return mLevels[level];
        }

        std::uint32_t getWidth() const { return mLevels.empty() ? 0 : mLevels.front().mWidth; }
        std::uint32_t getHeight() const { return mLevels.empty() ? 0 : mLevels.front().mHeight; }

        /// Where texel `(x, y)` of `level` begins, in a payload of `stride` bytes a texel.
        std::size_t offsetOf(std::uint32_t level, std::uint32_t x, std::uint32_t y, std::size_t stride) const
        {
            const MipLevel& which = getLevel(level);
            assert(x < which.mWidth && y < which.mHeight && "a texel outside its level");

            return which.texelOffset(x, y, stride);
        }

        /// Lays out a chain from `width` by `height` down to one texel, and answers how many bytes
        /// it needs laid as `laid` says. Every level's offset is in that payload.
        std::size_t layOutTo1x1(std::uint32_t width, std::uint32_t height, const TexelLayout& laid);

        /// Lays out one level per entry of `shape`, keeping their extents and renumbering their
        /// offsets into a payload laid as `laid` says, because the source's offsets are in the
        /// source's payload. Answers how many bytes that needs.
        std::size_t layOutLike(std::span<const MipLevel> shape, const TexelLayout& laid);
    };

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
        /// `describeImage` says why the header's alpha flag is not consulted.
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

        constexpr FormatTraits loose(
            TextureFormat format, std::string_view name, ChannelType type, std::array<std::int8_t, 4> from)
        {
            std::uint8_t count = 0;
            for (const std::int8_t channel : from)
                count = std::max<std::uint8_t>(count, static_cast<std::uint8_t>(channel + 1));
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
        FormatRows::packed(TextureFormat::Rgb565, "R5G6B5", ChannelBits{ .mRed = 5, .mGreen = 6, .mBlue = 5 }),
        FormatRows::packed(
            TextureFormat::Argb1555, "A1R5G5B5", ChannelBits{ .mAlpha = 1, .mRed = 5, .mGreen = 5, .mBlue = 5 }),
        FormatRows::packed(TextureFormat::Xrgb1555, "X1R5G5B5", ChannelBits{ .mRed = 5, .mGreen = 5, .mBlue = 5 }),
        FormatRows::packed(
            TextureFormat::Argb4444, "A4R4G4B4", ChannelBits{ .mAlpha = 4, .mRed = 4, .mGreen = 4, .mBlue = 4 }),
        FormatRows::packed(TextureFormat::Xrgb4444, "X4R4G4B4", ChannelBits{ .mRed = 4, .mGreen = 4, .mBlue = 4 }),
        FormatRows::loose(TextureFormat::Rgb8, "RGB8", ChannelType::Unorm8, FormatRows::sRgb),
        FormatRows::loose(TextureFormat::Bgr8, "BGR8", ChannelType::Unorm8, { 2, 1, 0, FormatRows::sNo }),
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

    /// What stands in a texture slot, which decides what a description carries and how a backend
    /// builds it. Stated rather than deduced from two sentinels, for the reason
    /// `Rtx::TextureKind` gives of the table a description is built from: a reader that has to
    /// work out which of four a value is works it out in its own order, and two orders are one
    /// rule written twice.
    enum class TextureSource : std::uint8_t
    {
        /// A file the content named, decoded. `TextureData::mBytes` and `mLevels` are the file's
        /// own, and `mCompleteChain` says whether the device finishes a chain it did not carry.
        File,

        /// The light bake of a sprite texture — `SpriteLightMap` says what a bake is — made on
        /// the device from that texture's alpha, `SpriteLightPass`. `TextureData::mFrom` is the
        /// slot it is made from, and it carries no bytes: a bake is shaped like its source.
        SpriteBake,

        /// A distant chunk's layer stack, flattened on the device by `GroundCompositePass` in the
        /// placement that writes the chunk's material row. `TextureData::mFrom` is that row, and
        /// it carries no bytes: a composite is `GROUND_COMPOSITE_EXTENT` square with a chain to
        /// one texel.
        GroundComposite,

        /// The same chunk's gloss, baked in the same pass from the same stack: how much of its
        /// ground reflects in red and how rough it is in green, as data. `TextureData::mFrom` is
        /// the chunk's row, and the image is shaped as its composite is.
        GroundGloss,

        /// What a slot is drawn as where what it names cannot stand: `describeStandIn`, whose
        /// bytes it carries. A backend stands one image for every such slot, and for a slot it has
        /// no room for, so a refusal costs the device nothing. Drawn only as a base colour: a reader
        /// of an optional map reads the slot as none, `TEXTURE_STANDS_IN`.
        StandIn,
    };

    /// A decoded texture, ready to upload and owning none of it. No graphics API in it, because an
    /// upload of a block-compressed file with its chain already built is a copy and never a
    /// conversion.
    struct TextureData
    {
        /// Which row of the backend's texture array this is, which is the slot a material holds.
        /// Carried rather than implied by position, because a slot a departing cell freed is taken
        /// over wherever it sits.
        Index mSlot = 0;

        /// How the slot is addressed past its edges, which is the sampler a backend binds it
        /// through. The scene's table says, per slot.
        TextureWrap mWrap = TextureWrap::Repeat;

        /// What stands in the slot, which says which of the fields below mean anything.
        TextureSource mSource = TextureSource::File;

        /// What the source names, by the source: the sprite texture a bake is made from, or the
        /// material row whose ground a composite or its gloss is. `sNoIndex` under `File` and
        /// `StandIn`, which are made from nothing but their own bytes.
        Index mFrom = sNoIndex;

        TextureFormat mFormat = TextureFormat::Bc1RgbaSrgb;

        /// What the slot's texels are, which the scene's table says per slot and `mFormat` follows.
        TextureEncoding mEncoding = TextureEncoding::Colour;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// Every level, back to back. The levels index into this. Empty under `SpriteBake`,
        /// `GroundComposite` and `GroundGloss`, whose bytes are the device's.
        std::span<const std::byte> mBytes{};
        std::span<const MipLevel> mLevels{};

        /// Whether a backend completes the chain the file did not carry, `MipChainPass`, from the
        /// one level here down to one texel. Set by the builder where `MipChain::wantedFor` says,
        /// and never for a texture a test paints to be read at its one level. Under `File` alone.
        bool mCompleteChain = false;

        /// What to call it in a capture — the file it came from. Spans storage the description's
        /// owner holds, like everything else here. Empty is allowed and only costs a nameless object
        /// in a debugger; every backend has somewhere to put it.
        std::string_view mName{};

        /// What stands beside it: the neutral shading map for a composite, whose painted light came
        /// off per tile in the bake and would come off twice; a bake, which nothing divides; the
        /// stand-in, which is one grey; and data, which is no picture of anything lit. A colour
        /// file's painted light, estimated on the device as it arrives (`ShadingPass`), and a normal
        /// map's spread, measured on it the same way (`NormalSpreadPass`). Derived, because the
        /// source and the encoding decide it.
        TextureCompanion getCompanion() const
        {
            if (mSource != TextureSource::File || mEncoding == TextureEncoding::Data)
                return TextureCompanion::Neutral;
            return mEncoding == TextureEncoding::Normal ? TextureCompanion::Spread : TextureCompanion::Shading;
        }

        /// The first level no wider and no taller than `side`, or nothing where every level is
        /// larger: where a texture held to that side begins. The levels halve, so every level
        /// after it is within the side too.
        std::optional<std::uint32_t> firstLevelWithin(std::uint32_t side) const
        {
            for (std::uint32_t level = 0; level < mLevels.size(); ++level)
                if (std::max(mLevels[level].mWidth, mLevels[level].mHeight) <= side)
                    return level;

            return std::nullopt;
        }

        /// How many bytes the levels from `level` on take: what an upload that begins there copies.
        std::size_t bytesFrom(std::uint32_t level) const
        {
            assert(level < mLevels.size() && "a level past the end of the chain");
            return mBytes.size() - mLevels[level].mOffset;
        }
    };

}
