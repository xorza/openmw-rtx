#pragma once

#include <cstdint>
#include <optional>

#include <osg/ref_ptr>

#include <components/contentstore/key.hpp>
#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/specularlayout.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/preprocess/contentpass.hpp>
#include <components/rtx/preprocess/filekeys.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// An image's finest level as a `TexturePass` reads it — `describeFinest` — for one run.
    ///
    /// **The image is held as long as its description**, because the description spans the image's
    /// own bytes wherever they need no laying out (`describeImage`).
    ///
    /// **The scratch is the preprocessor's, one for every texture pass**: a finest level widened
    /// to four bytes a texel is 16 MiB for a 2048-square picture, and a pass at a time runs on a
    /// preprocessor's thread. Not copyable: the description spans the scratch's buffers.
    class FinestTexels
    {
    public:
        explicit FinestTexels(TexelScratch& scratch);
        ~FinestTexels();

        FinestTexels(const FinestTexels&) = delete;
        FinestTexels& operator=(const FinestTexels&) = delete;

        /// Describes `image` into the scratch, and holds it until the next `describe` or `clear`.
        void describe(const osg::Image& image);

        /// What the last `describe` found, spanning the scratch or the held image: nothing for an
        /// image no reader here decodes.
        const std::optional<TextureData>& get() const { return mFinest; }

        TexelScratch& getScratch() { return mScratch; }

        /// Lets go of the image and its description.
        void clear();

    private:
        TexelScratch& mScratch;
        osg::ref_ptr<const osg::Image> mImage;
        std::optional<TextureData> mFinest;
    };

    /// A pass over a texture's finest level, keyed by its file (`FileKeys`), and `Answer` asked of the finest level
    /// described where the store holds no output. `Answer` is handed nothing for an image no reader here decodes.
    /// Handed an image that holds its texels: a header's facts are derived where
    /// its file is first read (`ContentPreprocessor::derive`).
    template <ContentPassId Id, std::uint32_t Version, class Result,
        Result (*Answer)(const std::optional<TextureData>& finest, TexelScratch& scratch)>
    class TexturePass
    {
    public:
        using Input = osg::Image;
        using Output = Result;

        static constexpr ContentPassId sPass = Id;
        static constexpr std::uint32_t sVersion = Version;

        void digest(const osg::Image& image, ContentStore::Digest& digest) { digest.add(mKeys.of(image)); }

        /// Answers for `image`, and lets go of it.
        void run(const osg::Image& image, Result& result)
        {
            mFinest.describe(image);
            result = Answer(mFinest.get(), mFinest.getScratch());
            mFinest.clear();
        }

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why — over the
        /// scratch it holds for every texture pass and the keys it asks.
        friend class ContentPreprocessor;
        TexturePass(TexelScratch& scratch, FileKeys& keys)
            : mFinest(scratch)
            , mKeys(keys)
        {
        }

        FinestTexels mFinest;
        FileKeys& mKeys;
    };

    /// Whether a texture's alpha ever reaches solid — `reachesSolid`. True for an image no reader
    /// here decodes, which is the answer that changes nothing about how the surface is traced.
    bool solidReachOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// What a texel of a texture is worth on average — `meanTexel`. Nothing for an image no reader
    /// here decodes.
    MeanTexel texelMeanOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// The lowest and the highest alpha a texture holds — `alphaRange`. The whole byte range for an image
    /// no reader here decodes, which bounds whatever it holds.
    AlphaRange alphaSpanOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// The most a classic `_spec` map's channels spread, on the mean: the Fullrest Repack's fourteen
    /// spread 0.004 to 0.042, and the PBR installs' 3400 metal-roughness maps 0.151 at the least, so a
    /// bound between them (`docs/architecture.md`, "One specular layout, a classic map converted").
    inline constexpr double sClassicSpreadMost = 0.1;

    /// What a `_spec` map's channels mean: classic where its texels' channels spread less than
    /// `sClassicSpreadMost` on the mean. Metal-roughness for an image no reader here decodes, which
    /// reads it as the data it would be.
    SpecularLayout specularKindOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// Asked of a translucent material's own diffuse map, whose texels it walks up to the first
    /// solid one.
    using SolidReach = TexturePass<ContentPassId::SolidReach, 1, bool, solidReachOf>;

    /// Asked of an additive material's own diffuse map, a sprite's and the sky's sheets, whose every
    /// texel it walks.
    using TexelMean = TexturePass<ContentPassId::TexelMean, 1, MeanTexel, texelMeanOf>;

    /// Asked of a normal map whose alpha is a height, whose every texel it walks: the heights its relief
    /// spans, which bound the march through it.
    using AlphaSpan = TexturePass<ContentPassId::AlphaSpan, 1, AlphaRange, alphaSpanOf>;

    /// Asked of every `_spec` map a material wears, whose every texel it reads.
    using SpecularKind = TexturePass<ContentPassId::SpecularKind, 1, SpecularLayout, specularKindOf>;
}
