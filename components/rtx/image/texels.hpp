#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <osg/Vec3f>

#include "texturedata.hpp"
#include "textureencoding.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    struct AlphaScratch;

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

    /// What a file's texels say, read in one walk over its finest level. The finest level alone,
    /// because a mask's average stops reaching solid a level or two down. A file's texels never
    /// change, so `ImageFactCache` keeps the answer for its thread's life.
    struct ImageFacts
    {
        MeanTexel mMean;

        /// Whether any texel is fully opaque — what tells a wisp from a mask, since Morrowind keeps
        /// its foliage and its clouds under one alpha mode: a leaf card is solid wherever its paint
        /// is, and `Tx_Dagoth_Cloud`'s alpha peaks at seven fifteenths.
        bool mReachesSolid = true;
    };

    /// The facts of a finest level `describeFinest` gave, from every texel and not a sample: a mean
    /// of a sheet that is mostly empty cannot be sampled, and one solid texel makes a mask.
    /// The alpha and the colours are read into `scratch`.
    ImageFacts imageFactsOf(const TextureData& finest, AlphaScratch& scratch);

    /// Which format `image` arrived in, read as `encoding` — the one place a texture's `GLenum`
    /// decides its format, so the uploader and the report cannot disagree. A blend map is weights
    /// and not a texture, and `GroundReader` reads its bytes itself.
    TextureFormat readFormat(const osg::Image& image, TextureEncoding encoding = TextureEncoding::Colour);

    /// Whether a normal map bound for its height has one: an alpha, which a map of two channels
    /// has not. The rasterizer's `ShaderVisitor` and `Terrain` turn parallax off for the same maps,
    /// where the alpha a sampler returns is one and the shift would be the same everywhere.
    bool carriesHeight(const osg::Image& normalMap);

    /// What `format` is called, for a report to print.
    std::string_view nameOf(TextureFormat format);
}
