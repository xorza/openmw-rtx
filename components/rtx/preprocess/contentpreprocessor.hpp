#pragma once

#include <components/rtx/image/alphaimage.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>

#include "contentcache.hpp"
#include "contentpass.hpp"
#include "contentstats.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// The one way the renderer computes anything from what the content files hold. Every pass is
    /// asked through here: its key is made of everything it reads, the cache is asked for the
    /// output under that key, and the pass runs only where the cache has none — and then the cache
    /// is offered what it computed. `ContentCache` holds nothing, so for now every pass runs, and
    /// what each cost is counted.
    ///
    /// **One a thread**, as the passes' scratch is: the frame's walk has one and the cell ring's
    /// reader another. Not copyable, because the passes hold its scratch by reference.
    class ContentPreprocessor
    {
    public:
        ContentPreprocessor() = default;

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;

        /// `ShapePass`: what a drawable's triangles come to, and its normals where they split.
        void shape(const ShapePass::Input& input, ShapePass::Output& output);

        /// `ImageFactPass`: what `image`'s texels say. A walk over every texel of its finest level,
        /// so a caller asks `ImageFactCache`, which asks this once a file.
        ImageFacts imageFacts(const osg::Image& image);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

    private:
        template <ContentPass Pass>
        void run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output);

        /// What the texture pass describes an image into.
        AlphaScratch mAlphaScratch;

        ShapePass mShape;
        ImageFactPass mImageFacts{ mAlphaScratch };

        ContentCache mCache;
        ContentStats mStats;
    };
}
