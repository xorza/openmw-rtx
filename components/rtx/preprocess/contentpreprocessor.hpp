#pragma once

#include <vector>

#include <components/rtx/image/alpharange.hpp>
#include <components/rtx/image/chainbuild.hpp>
#include <components/rtx/image/texels.hpp>
#include <components/rtx/preprocess/shape/shapepass.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/chainscratch.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/rtx/preprocess/texture/texturepass.hpp>

#include "contentcache.hpp"
#include "contentpass.hpp"
#include "contentstats.hpp"
#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>

namespace osg
{
    class Image;
}

namespace Rtx
{
    /// What an ask of a stored pass wants: the output, from the store or a run, or only that the
    /// store holds it — a preparation's ask, which reads no entry the store holds, and hands the store
    /// what a run made, the output left emptied where its codec takes it whole.
    enum class ContentAsk
    {
        Output,
        Stored,
    };

    /// The one way the renderer computes anything from what the content files hold. Every pass is
    /// asked through here: for a pass the store keeps (`ContentPass::sStored`) where there is a
    /// store, its key is made of everything it reads, the cache is asked for the output under that
    /// key, and the pass runs only where the cache has none — and then the cache is offered what it
    /// computed. Every other pass runs. What each cost is counted.
    ///
    /// **One a thread**, as the passes' scratch is: the frame's walk has one and the cell ring's
    /// reader another. Pinned: the texture passes describe into the one scratch it holds for them, and
    /// the chain and conversion passes make their chains in its one build.
    class ContentPreprocessor
    {
    public:
        /// @param store the process's store, or null for none (`ContentCache`): a test's, which
        ///        computes every ask.
        /// @param reach where this thread's asks may reach in it.
        explicit ContentPreprocessor(
            ContentStore::Store* store = nullptr, ContentStore::Reach reach = ContentStore::Reach::Memory);

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

        /// `SpecularKind`: what `image`'s channels mean as a `_spec` map.
        SpecularLayout specularLayout(const osg::Image& image);

        /// `ChainPass`: an image's chain made again, or made where it carried none, read as its input says.
        void chain(const ChainPass::Input& input, MadeChain& made, ContentAsk ask = ContentAsk::Output);

        /// `ConversionPass`: a classic pair's base colour or metal-roughness map.
        void convert(const ConversionPass::Input& input, MadeChain& made, ContentAsk ask = ContentAsk::Output);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

        /// Appends to `into`, until it is handed null, the key of every entry an ask of a stored pass makes
        /// or finds: what a preparation files a model's walk under, so a later start asks the store for
        /// each (`PreparationManifest`). Null, as made, for none.
        void collectKeys(std::vector<StoredKey>* into) { mKeysInto = into; }

        /// What its chain and conversion buffers keep, the shared build's among them: every buffer of a
        /// `ChainScratch` but the made chain, which the caller holds, and the payload, which the store frees.
        ChainScratch chainScratch() const;

        /// Gives back the room its chain and conversion buffers keep, the build's among them.
        void letGoChainScratch();

    private:
        template <ContentPass Pass>
        void run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output,
            ContentAsk ask = ContentAsk::Output);

        ShapePass mShape;
        TexelScratch mTexels;
        SolidReach mSolid{ mTexels };
        TexelMean mMean{ mTexels };
        SolidFall mFall{ mTexels };
        AlphaSpan mSpan{ mTexels };
        SpecularKind mKind{ mTexels };
        ChainBuild mBuild;
        ChainPass mChain{ mBuild };
        ConversionPass mConversion{ mBuild };

        ContentCache mCache;
        ContentStats mStats;
        std::vector<StoredKey>* mKeysInto = nullptr;
    };
}
