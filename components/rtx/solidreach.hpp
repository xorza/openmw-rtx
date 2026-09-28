#pragma once

#include <cstdint>

#include "contentpass.hpp"
#include "finesttexels.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// Whether a texture's alpha ever reaches solid, as a pass — `reachesSolid`. Asked of a
    /// translucent material's own diffuse map, whose texels it walks up to the first solid one.
    class SolidReach
    {
    public:
        using Input = osg::Image;
        using Output = bool;

        static constexpr ContentPassId sPass = ContentPassId::SolidReach;
        static constexpr std::uint32_t sVersion = 1;

        void digest(const osg::Image& image, ContentDigest& digest) { mFinest.describe(image, digest); }

        /// Answers for the image the last `digest` described. True for one no reader here decodes,
        /// which is the answer that changes nothing about how the surface is traced.
        void run(const osg::Image& image, bool& solid);

    private:
        /// Made by a `ContentPreprocessor` and by nothing else — `ShapeFold` says why.
        friend class ContentPreprocessor;
        explicit SolidReach(AlphaScratch& scratch)
            : mFinest(scratch)
        {
        }

        FinestTexels mFinest;
    };
}
