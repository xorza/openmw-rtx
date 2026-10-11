#pragma once

#include <algorithm>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include <components/rtx/common/index.hpp>

#include "textureencoding.hpp"
#include "textureformat.hpp"
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
    /// The largest side of a file whose completed chain stays loose (`TextureData::encodesChain`):
    /// the largest of the hundred and eighty-seven single-level files Morrowind ships, so no
    /// vanilla picture moves.
    inline constexpr std::uint32_t sLargestLooseChainSide = 512;

    inline std::uint32_t levelsTo1x1(std::uint32_t width, std::uint32_t height)
    {
        return static_cast<std::uint32_t>(std::bit_width(std::max(width, height)));
    }

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

        /// The light bake of a sprite texture — `TextureKind::SpriteLight` says what a bake is —
        /// made on the device from that texture's alpha. `TextureData::mFrom` is the slot it is made
        /// from, and it carries no bytes: a bake is shaped like its source.
        SpriteBake,

        /// A chunk's layer stack, flattened on the device in the placement that writes the
        /// chunk's material row. `TextureData::mFrom` is that row, and it carries no bytes: a
        /// composite is `GROUND_COMPOSITE_EXTENT` square with a chain to one texel.
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

    /// What the textures a backend stands come to on the device, by what stands in each slot: which
    /// of them a memory budget is spent on. The stand-in costs a slot nothing and is not here.
    struct TextureSourceBytes
    {
        /// A file standing as the content carried its chain, and one whose chain the device completed
        /// from its one level (`TextureData::mCompleteChain`).
        std::uint64_t mFiles = 0;
        std::uint64_t mCompletedFiles = 0;

        std::uint64_t mSpriteBakes = 0;

        /// The ground's composites and the canvas they are baked on, and their gloss.
        std::uint64_t mGroundComposites = 0;
        std::uint64_t mGroundGloss = 0;
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

        /// Whether a backend completes the chain the file did not carry, on the device, from the
        /// one level here down to one texel. Set by the builder where `wantsCompletedChain` says,
        /// and never for a texture a test paints to be read at its one level. Under `File` alone.
        bool mCompleteChain = false;

        /// What to call it in a capture — the file it came from. Spans storage the description's
        /// owner holds, like everything else here. Empty is allowed and only costs a nameless object
        /// in a debugger; every backend has somewhere to put it.
        std::string_view mName{};

        /// What stands beside it: the neutral shading map for a composite, whose painted light came
        /// off per tile in the bake and would come off twice; a bake, which nothing divides; the
        /// stand-in, which is one grey; and data, which is no picture of anything lit. A colour
        /// file's painted light, estimated on the device as it arrives, and a normal map's spread,
        /// measured on it the same way. Derived, because the source and the encoding decide it.
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

        /// Whether this is a file whose chain a backend completes: one level, and more than a texel.
        ///
        /// **Only a file that carried no chain at all.** Morrowind ships a hundred and eighty-seven
        /// such textures and its rain is one: read at its finest, a drop's peak alpha is 0.400 where
        /// the missing levels hold 0.283, 0.129 and 0.068, so a storm comes out as hard white marks
        /// that flicker; the rasterizer has the driver generate them. Morrowind's own chains stop
        /// short of a single texel — a 256-square texture ships six levels and ends at 8 by 8, and
        /// that last level is already the texture's own mean to within what a ray can tell —
        /// and rebuilding those would decompress the whole game to gain nothing. A texel has no
        /// level below it, and a level with no extent has no texel to read. **Completed in loose
        /// texels** up to `sLargestLooseChainSide`, and not compressed again, because a level in
        /// another format is a level read differently: `Bc7EncodePass` writes every level of an
        /// image, the file's own among them, and its mode 6 shares one low bit across an endpoint's
        /// channels, which a decoded BC1 or BC3 texel does not survive — a vanilla picture moved for
        /// a renderer's bookkeeping. Past that side, `encodesChain` says.
        bool wantsCompletedChain() const;

        /// Whether a chain completed for this file is encoded to BC7 rather than kept loose: a side
        /// past `sLargestLooseChainSide`, where no vanilla file stands. A replacer's 4096-square BC1
        /// with no levels is 8 MB on disk, 89 MB as a loose chain and 22 MB as a BC7 one, and the
        /// budget cannot stand a completed chain from a coarser level. Its own level moves by the
        /// low bit the comment above names.
        bool encodesChain() const;

        /// Whether every level lies inside `mBytes` at its format's layout: what `describeImage`
        /// guarantees and a reader of the bytes asserts rather than clamps, so a description short
        /// of its bytes fails where it is read and is not read as opaque or as nothing.
        bool levelsFit() const;
    };

}
