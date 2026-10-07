#pragma once

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
    /// asked through here: once a cache holds anything, its key is made of everything it reads, the
    /// cache is asked for the output under that key, and the pass runs only where the cache has none
    /// — and then the cache is offered what it computed. `ContentCache` holds nothing, so for now no
    /// key is made, every pass runs, and what each cost is counted.
    ///
    /// **One a thread**, as the passes' scratch is: the frame's walk has one and the cell ring's
    /// reader another. Not copyable: each texture pass's description spans its own scratch.
    class ContentPreprocessor
    {
    public:
        ContentPreprocessor() = default;

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;

        /// `ShapePass`: what a drawable's triangles come to, and its normals where they split.
        void shape(const ShapePass::Input& input, ShapePass::Output& output);

        /// `SolidReach`: whether `image`'s alpha ever reaches solid.
        bool reachesSolid(const osg::Image& image);

        /// `TexelMean`: what a texel of `image` is worth on average.
        MeanTexel meanTexel(const osg::Image& image);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

    private:
        template <ContentPass Pass>
        void run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output);

        ShapePass mShape;
        SolidReach mSolid;
        TexelMean mMean;

        ContentCache mCache;
        ContentStats mStats;
    };
}
