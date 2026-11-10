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

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// An image's finest level as a `TexturePass` reads it — `describeFinest` — described once for
    /// both the key and the run that follows it, where a key is made.
    ///
    /// **The key is what the passes read and nothing else**: the format, the encoding, the finest
    /// level's extent and its bytes. Not the file's name, so one picture under two names is one
    /// entry; not the coarser levels, which no texture pass reads; and not the size OpenSceneGraph
    /// counts the whole image at, which differs between its builds for a block format with no
    /// chain. An image that does not describe is keyed as that, because what a pass answers for it
    /// does not depend on its bytes.
    ///
    /// **The image is held as long as its description**, because the description spans the image's
    /// own bytes wherever they need no laying out (`describeImage`), and a key and the run after it
    /// are two calls apart: a caller that let go of its image between them left the key reading
    /// freed memory. Held, its address also names it, so a run knows whether the description is of
    /// the image it was handed. A key the cache answers runs nothing, and leaves its image held
    /// until the pass is next asked: one image a pass, on a loader's thread.
    ///
    /// **The scratch is the preprocessor's, one for both texture passes**: a finest level widened
    /// to four bytes a texel is 16 MiB for a 2048-square picture, and a pass at a time runs on a
    /// preprocessor's thread, from its key to its run, and lets go of its description as it
    /// answers. Not copyable: the description spans the scratch's buffers.
    class FinestTexels
    {
    public:
        explicit FinestTexels(TexelScratch& scratch);
        ~FinestTexels();

        FinestTexels(const FinestTexels&) = delete;
        FinestTexels& operator=(const FinestTexels&) = delete;

        /// Describes `image` into the scratch, and holds it until the next `describe` or `clear`.
        void describe(const osg::Image& image);

        /// Whether the last `describe` was of `image`.
        bool describes(const osg::Image& image) const { return mImage.get() == &image; }

        /// Adds what the last `describe` came to to `digest`.
        void addTo(ContentStore::Digest& digest) const;

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

    /// A pass over a texture's finest level: `FinestTexels` keys it, and `Answer` is asked of what
    /// the key described, so an image is read once for both. `Answer` is handed nothing for an image
    /// no reader here decodes. `Stored` is `ContentPass::sStored`.
    template <ContentPassId Id, std::uint32_t Version, bool Stored, class Result,
        Result (*Answer)(const std::optional<TextureData>& finest, TexelScratch& scratch)>
    class TexturePass
    {
    public:
        using Input = osg::Image;
        using Output = Result;

        static constexpr ContentPassId sPass = Id;
        static constexpr std::uint32_t sVersion = Version;
        static constexpr bool sStored = Stored;

        void digest(const osg::Image& image, ContentStore::Digest& digest)
        {
            mFinest.describe(image);
            mFinest.addTo(digest);
        }

        /// Answers for `image`, described by the `digest` before it where that was of `image`, and
        /// lets go of it.
        void run(const osg::Image& image, Result& result)
        {
            if (!mFinest.describes(image))
                mFinest.describe(image);
            result = Answer(mFinest.get(), mFinest.getScratch());
            mFinest.clear();
        }

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why — over the
        /// scratch it holds for both texture passes.
        friend class ContentPreprocessor;
        explicit TexturePass(TexelScratch& scratch)
            : mFinest(scratch)
        {
        }

        FinestTexels mFinest;
    };

    /// Whether a texture's alpha ever reaches solid — `reachesSolid`. True for an image no reader
    /// here decodes, which is the answer that changes nothing about how the surface is traced.
    bool solidReachOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// What a texel of a texture is worth on average — `meanTexel`. Nothing for an image no reader
    /// here decodes.
    MeanTexel texelMeanOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// Whether a texture's alpha ever falls below solid — `fallsBelowSolid`. False for an image no
    /// reader here decodes, which is the answer that changes nothing about how the surface is traced.
    bool solidFallOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// The lowest and the highest alpha a texture holds — `alphaRange`. The whole byte range for an image
    /// no reader here decodes, which bounds whatever it holds.
    AlphaRange alphaSpanOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// The most a classic `_spec` map's channels spread, on the mean: the Fullrest Repack's fourteen
    /// spread 0.004 to 0.042, and the PBR installs' 3400 metal-roughness maps 0.151 at the least, so a
    /// bound between them (`docs/rtx/architecture.md`, "One specular layout, a classic map converted").
    inline constexpr double sClassicSpreadMost = 0.1;

    /// What a `_spec` map's channels mean: classic where its texels' channels spread less than
    /// `sClassicSpreadMost` on the mean. Metal-roughness for an image no reader here decodes, which
    /// reads it as the data it would be.
    SpecularLayout specularKindOf(const std::optional<TextureData>& finest, TexelScratch& scratch);

    /// Asked of a translucent material's own diffuse map, whose texels it walks up to the first
    /// solid one.
    using SolidReach = TexturePass<ContentPassId::SolidReach, 1, false, bool, solidReachOf>;

    /// Asked of an additive material's own diffuse map, a sprite's and the sky's sheets, whose every
    /// texel it walks.
    using TexelMean = TexturePass<ContentPassId::TexelMean, 1, true, MeanTexel, texelMeanOf>;

    /// Asked of a blended material's dark map, whose texels it walks up to the first one under
    /// solid.
    using SolidFall = TexturePass<ContentPassId::SolidFall, 1, false, bool, solidFallOf>;

    /// Asked of a normal map whose alpha is a height, whose every texel it walks: the heights its relief
    /// spans, which bound the march through it.
    using AlphaSpan = TexturePass<ContentPassId::AlphaSpan, 1, true, AlphaRange, alphaSpanOf>;

    /// Asked of every `_spec` map a material wears, whose every texel it reads.
    using SpecularKind = TexturePass<ContentPassId::SpecularKind, 1, true, SpecularLayout, specularKindOf>;
}
