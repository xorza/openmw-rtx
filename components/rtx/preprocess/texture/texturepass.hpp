#pragma once

#include <cstdint>
#include <optional>

#include <components/rtx/image/texels.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    class ContentDigest;
    struct AlphaScratch;

    /// An image's finest level as a `TexturePass` reads it — `describeFinest` — described once for
    /// both the key and the run that follows it.
    ///
    /// **The key is what the passes read and nothing else**: the format, the encoding, the finest
    /// level's extent and its bytes. Not the file's name, so one picture under two names is one
    /// entry; not the coarser levels, which no texture pass reads; and not the size OpenSceneGraph
    /// counts the whole image at, which differs between its builds for a block format with no
    /// chain. An image that does not describe is keyed as that, because what a pass answers for it
    /// does not depend on its bytes.
    class FinestTexels
    {
    public:
        explicit FinestTexels(AlphaScratch& scratch)
            : mScratch(scratch)
        {
        }

        /// Describes `image` into the scratch and adds what it came to to `digest`.
        void describe(const osg::Image& image, ContentDigest& digest);

        /// What the last `describe` found, spanning the scratch: nothing for an image no reader
        /// here decodes.
        const std::optional<TextureData>& get() const { return mFinest; }

        AlphaScratch& getScratch() const { return mScratch; }

    private:
        AlphaScratch& mScratch;
        std::optional<TextureData> mFinest;
    };

    /// A pass over a texture's finest level: `FinestTexels` keys it, and `Answer` is asked of what
    /// the key described, so an image is read once for both. `Answer` is handed nothing for an image
    /// no reader here decodes.
    template <ContentPassId Id, std::uint32_t Version, class Result,
        Result (*Answer)(const std::optional<TextureData>& finest, AlphaScratch& scratch)>
    class TexturePass
    {
    public:
        using Input = osg::Image;
        using Output = Result;

        static constexpr ContentPassId sPass = Id;
        static constexpr std::uint32_t sVersion = Version;

        void digest(const osg::Image& image, ContentDigest& digest) { mFinest.describe(image, digest); }

        /// Answers for the image the last `digest` described.
        void run(const osg::Image&, Result& result) { result = Answer(mFinest.get(), mFinest.getScratch()); }

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why.
        friend class ContentPreprocessor;
        explicit TexturePass(AlphaScratch& scratch)
            : mFinest(scratch)
        {
        }

        FinestTexels mFinest;
    };

    /// What a texture's texels say — `imageFactsOf`. For an image no reader here decodes, the
    /// default facts: a mean of nothing, and an alpha that reaches solid, which is the answer that
    /// changes nothing about how the surface is traced.
    ImageFacts factsOfFinest(const std::optional<TextureData>& finest, AlphaScratch& scratch);

    /// Asked of a blended material's own diffuse map, a sprite's and the sky's sheets, whose every
    /// texel it walks.
    using ImageFactPass = TexturePass<ContentPassId::ImageFacts, 1, ImageFacts, factsOfFinest>;
}
