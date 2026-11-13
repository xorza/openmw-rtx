#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "colourblock.hpp"
#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    struct TexelScratch;

    /// The colour half of the block that begins at `block`: its last eight bytes whichever format it
    /// is, because BC2 and BC3 put their alpha in front of it and BC1 has none.
    inline std::span<const std::byte, 8> colourHalfAt(
        const std::span<const std::byte> bytes, const std::size_t block, const TexelLayout& layout)
    {
        return bytes.subspan(block + (layout.mBytes - 8)).first<8>();
    }

    /// The three colours of the loose texel of `texture` that begins at `at`, a byte each over 255,
    /// in red, green and blue order whichever order the format states them in: a reader that took
    /// one order for both draws the sky with its red and blue swapped. Defined here because a walk of a
    /// loose level asks it once a texel.
    inline osg::Vec3f looseColourAt(const TextureData& texture, const std::size_t at)
    {
        assert(layoutOf(texture.mFormat).mBytes == 4 && "a loose texel read as four bytes that is not");
        const auto channel
            = [&](std::size_t offset) { return std::to_integer<std::uint32_t>(texture.mBytes[at + offset]) / 255.0f; };

        // The two loose spellings differ only in which end the three colours are stated from.
        if (isBgr(texture.mFormat))
            return osg::Vec3f(channel(2), channel(1), channel(0));

        return osg::Vec3f(channel(0), channel(1), channel(2));
    }

    /// The loose texel `x`, `y` of `level`, whose format `layout` lays.
    inline osg::Vec3f looseTexel(const TextureData& texture, const MipLevel& level, const TexelLayout& layout,
        const std::uint32_t x, const std::uint32_t y)
    {
        return looseColourAt(texture, level.texelOffset(x, y, layout.mBytes));
    }

    /// The colours of the block at `column` and `band` of `level`, counted in blocks.
    inline ColourBlock colourBlockAt(const TextureData& texture, const MipLevel& level, const TexelLayout& layout,
        const std::uint32_t column, const std::uint32_t band)
    {
        return ColourBlock::read(colourHalfAt(texture.mBytes, level.blockOffset(column, band, layout.mBytes), layout),
            isBc1(texture.mFormat));
    }

    /// Whether `texelAt` and `readTexelBand` can read `texture`'s colour: a description that carries
    /// its own bytes, in a BC1, BC2 or BC3 block or four loose bytes. A bake and a composite carry no
    /// bytes, since the device makes them, and BC5 holds two data channels and no colour.
    bool readsColour(const TextureData& texture);

    /// The colour of one texel of one level, as it is stored — display-encoded; `Rtx::toLinear`
    /// turns it into light. For a reader of a scattered few: a texel of a block decodes the whole
    /// block, and a reader of every texel takes `readTexelBand`. `texture` is one `readsColour`
    /// answers yes for, and `x` and `y` lie inside `level`.
    osg::Vec3f texelAt(const TextureData& texture, const MipLevel& level, std::uint32_t x, std::uint32_t y);

    /// How many rows band `band` of `level` holds: four, and fewer at the level's bottom edge.
    constexpr std::uint32_t bandRows(const MipLevel& level, std::uint32_t band)
    {
        assert(band * 4 < level.mHeight && "a band below the level");
        return std::min(level.mHeight - band * 4, 4u);
    }

    /// Rows `4 × band` to `4 × band + 3` of `level` (`bandRows`), its stored colours handed through `turn` four at
    /// a time and what it made of each handed to `place(x, row, turned)` for each texel that holds the colour,
    /// `row` counted from the band's first. **The four are a block's palette, and not its sixteen texels**, so a
    /// turn that costs is paid a quarter as often, and one that works four colours together works a palette at
    /// once; of a loose level they are four texels along a row, a row's last few beside colours of nought, whose
    /// turns are placed nowhere. A block's texels are placed before the next block's. `texture` is one
    /// `readsColour` answers yes for.
    ///
    /// @param turn `std::array<T, 4>(const std::array<osg::Vec3f, 4>&)`
    template <class Turn, class Place>
    void walkBand(const TextureData& texture, const MipLevel& level, const std::uint32_t band, Turn turn, Place place)
    {
        assert(readsColour(texture) && "a band read of a texture with no colour to read");
        assert(texture.levelsFit() && "a band read of a description short of its bytes");
        const std::uint32_t rows = bandRows(level, band);
        const std::uint32_t width = level.mWidth;

        const TexelLayout layout = layoutOf(texture.mFormat);
        if (!layout.isBlocked())
        {
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t first = 0; first < width; first += 4)
                {
                    const std::uint32_t across = std::min(width - first, 4u);
                    std::array<osg::Vec3f, 4> stored;
                    for (std::uint32_t x = 0; x < across; ++x)
                        stored[x] = looseTexel(texture, level, layout, first + x, band * 4 + row);
                    const auto turned = turn(stored);
                    for (std::uint32_t x = 0; x < across; ++x)
                        place(first + x, row, turned[x]);
                }
            return;
        }

        const std::uint32_t columns = (width + 3) / 4;
        for (std::uint32_t column = 0; column < columns; ++column)
        {
            const ColourBlock block = colourBlockAt(texture, level, layout, column, band);
            const auto turned = turn(block.mPalette);

            const std::uint32_t across = std::min(width - column * 4, 4u);
            for (std::uint32_t row = 0; row < rows; ++row)
                for (std::uint32_t x = 0; x < across; ++x)
                    place(column * 4 + x, row, turned[block.indexAt(std::size_t{ row } * 4 + x)]);
        }
    }

    /// Rows `4 × band` to `4 × band + 3` of `level` (fewer at its bottom edge) as they are stored, with every block of
    /// them decoded once where `texelAt` decodes one for each texel it hands out. Every texel a band holds is
    /// `texelAt`'s to the bit. `texture` is one `readsColour` answers yes for.
    ///
    /// @param into refilled with `level.mWidth` texels a row.
    void readTexelBand(
        const TextureData& texture, const MipLevel& level, std::uint32_t band, std::vector<osg::Vec3f>& into);

    /// The same band in light, `Rtx::toLinear` of each texel: a block's four palette entries are
    /// turned once, where a texel at a time turned up to sixteen, and an interpolated entry is
    /// never a stored byte the table answers.
    void readLightBand(
        const TextureData& texture, const MipLevel& level, std::uint32_t band, std::vector<osg::Vec3f>& into);

    /// What one texel of an image is worth on average.
    struct MeanTexel
    {
        /// The mean colour, linear and premultiplied by its own alpha — what a sheet is worth as a
        /// light, since it is drawn as `rgb * a` over a known piece of sky.
        osg::Vec3f mColour;

        /// The mean colour with the alpha unread — what a sheet is worth where it adds whole,
        /// `BlendKind::AddWhole`, which reads none.
        osg::Vec3f mWhole;

        /// The mean of its own alpha — what tells a wisp from a lid, since a few bright clouds over
        /// an empty sky average to the same colour as a solid grey one.
        float mAlpha = 0.0f;

        /// The mean of what that alpha calls solid: `mColour` with the cover divided back out, which
        /// is what a texel is read as a ratio to where the painting is used for its shape. Nothing
        /// where nothing is painted.
        osg::Vec3f opaque() const;
    };

    /// Averages a finest level `describeFinest` gave, every texel and not a sample, because a mean
    /// of a sheet that is mostly empty cannot be sampled. The alpha and the colours are read into
    /// `scratch`.
    MeanTexel meanTexel(const TextureData& finest, TexelScratch& scratch);
}
