#pragma once

#include <cstdint>

#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/contentpass.hpp>

#include "finesttexels.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// What a texel of a texture is worth on average, as a pass — `meanTexel`. Asked of an additive
    /// material's own diffuse map, a sprite's and the sky's sheets, whose every texel it walks.
    class TexelMean
    {
    public:
        using Input = osg::Image;
        using Output = MeanTexel;

        static constexpr ContentPassId sPass = ContentPassId::TexelMean;
        static constexpr std::uint32_t sVersion = 1;

        void digest(const osg::Image& image, ContentDigest& digest) { mFinest.describe(image, digest); }

        /// Averages the image the last `digest` described. Nothing for one no reader here decodes.
        void run(const osg::Image& image, MeanTexel& mean);

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why.
        friend class ContentPreprocessor;
        explicit TexelMean(AlphaScratch& scratch)
            : mFinest(scratch)
        {
        }

        FinestTexels mFinest;
    };
}
