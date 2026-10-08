#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <osg/Vec3f>

#include "texturedata.hpp"
#include "textureformat.hpp"

namespace Rtx
{
    struct TexelScratch;

    /// The colour half of the block that begins at `block`: its last eight bytes whichever format it
    /// is, because BC2 and BC3 put their alpha in front of it and BC1 has none.
    std::span<const std::byte, 8> colourHalfAt(
        std::span<const std::byte> bytes, std::size_t block, const TexelLayout& layout);

    /// The three colours of the loose texel of `texture` that begins at `at`, a byte each over 255,
    /// in red, green and blue order whichever order the format states them in: a reader that took
    /// one order for both draws the sky with its red and blue swapped.
    osg::Vec3f looseColourAt(const TextureData& texture, std::size_t at);

    /// Whether `texelAt` and `readTexelBand` can read `texture`'s colour: a description that carries
    /// its own bytes, in a BC1, BC2 or BC3 block or four loose bytes. A bake and a composite carry no
    /// bytes, since the device makes them, and BC5 holds two data channels and no colour.
    bool readsColour(const TextureData& texture);

    /// The colour of one texel of one level, as it is stored — display-encoded; `Rtx::toLinear`
    /// turns it into light. For a reader of a scattered few: a texel of a block decodes the whole
    /// block, and a reader of every texel takes `readTexelBand`. `texture` is one `readsColour`
    /// answers yes for, and `x` and `y` lie inside `level`.
    osg::Vec3f texelAt(const TextureData& texture, const MipLevel& level, std::uint32_t x, std::uint32_t y);

    /// Rows `4 × band` to `4 × band + 3` of `level` — fewer at its bottom edge — as they are
    /// stored, row after row, with every block of them decoded once where `texelAt` decodes one
    /// for each texel it hands out: sixteen times over for a reader of every texel. A band is a
    /// row of blocks, and every texel a band holds is `texelAt`'s to the bit. `texture` is one
    /// `readsColour` answers yes for.
    ///
    /// @param into refilled with `level.mWidth` texels a row.
    void readTexelBand(
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
