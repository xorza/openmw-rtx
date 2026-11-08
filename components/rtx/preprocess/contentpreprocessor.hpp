#pragma once

#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>

#include "contentcache.hpp"
#include "contentpass.hpp"
#include "contentstats.hpp"
#include "contentstore.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// The one way the renderer computes anything from what the content files hold. Every pass is
    /// asked through here: for a pass the store keeps (`ContentPass::sStored`) where there is a
    /// store, its key is made of everything it reads, the cache is asked for the output under that
    /// key, and the pass runs only where the cache has none — and then the cache is offered what it
    /// computed. Every other pass runs. What each cost is counted.
    ///
    /// **One a thread**, as the passes' scratch is: the frame's walk has one and the cell ring's
    /// reader another. Pinned: the texture passes describe into the one scratch it holds for both.
    class ContentPreprocessor
    {
    public:
        /// @param store the process's store, or null for none (`ContentCache`): a test's, which
        ///        computes every ask.
        /// @param reach where this thread's asks may reach in it.
        explicit ContentPreprocessor(ContentStore* store = nullptr, StoreReach reach = StoreReach::Memory);

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;
        ContentPreprocessor(ContentPreprocessor&&) = delete;
        ContentPreprocessor& operator=(ContentPreprocessor&&) = delete;

        /// `ShapePass`: what a drawable's triangles come to, and its normals where they split.
        void shape(const ShapePass::Input& input, ShapePass::Output& output);

        /// `SolidReach`: whether `image`'s alpha ever reaches solid.
        bool reachesSolid(const osg::Image& image);

        /// `TexelMean`: what a texel of `image` is worth on average.
        MeanTexel meanTexel(const osg::Image& image);

        /// `SolidFall`: whether `image`'s alpha ever falls below solid.
        bool fallsBelowSolid(const osg::Image& image);

        /// `AlphaSpan`: the lowest and the highest alpha `image` holds.
        AlphaRange alphaRange(const osg::Image& image);

        /// `ChainPass`: an image's chain made again, or made where it carried none, read as its input says.
        void chain(const ChainPass::Input& input, MadeChain& made);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

    private:
        template <ContentPass Pass>
        void run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output);

        ShapePass mShape;
        TexelScratch mTexels;
        SolidReach mSolid{ mTexels };
        TexelMean mMean{ mTexels };
        SolidFall mFall{ mTexels };
        AlphaSpan mSpan{ mTexels };
        ChainPass mChain;

        ContentCache mCache;
        ContentStats mStats;
    };
}
