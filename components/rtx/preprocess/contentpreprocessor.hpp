#pragma once

#include <cstdint>
#include <stop_token>
#include <vector>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/rtx/common/bufferpool.hpp>
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
#include "filekeys.hpp"
#include "headerderived.hpp"

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

    /// Which runs of the chain and conversion passes a preprocessor makes where the store holds no output: every one,
    /// or those the system has the memory for (`admitsChain`), asked only past the store's miss, so a chain the store
    /// holds is read whatever its making would cost.
    enum class ChainRuns : std::uint8_t
    {
        Always,
        WithinMemory,
    };

    /// A made chain as the device is handed it: its form, and its bytes in a buffer of a `BufferPool`, empty where it
    /// stands.
    struct PooledChain
    {
        ChainForm mForm = ChainForm::Stands;
        PooledBuffer mBytes;
    };

    /// The one way the renderer computes anything from what the content files hold. Every pass is
    /// asked through here: where there is a store, its key is made of everything it reads, the cache
    /// is asked for the output under that key, and the pass runs only where the cache has none — and
    /// then the cache is offered what it computed. With no store every pass runs. What each cost is
    /// counted.
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
        /// @param runs which chains and conversions it makes where the store holds none.
        explicit ContentPreprocessor(ContentStore::Store* store = nullptr,
            ContentStore::Reach reach = ContentStore::Reach::Memory, ChainRuns runs = ChainRuns::Always);

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;
        ~ContentPreprocessor() = default;

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

        /// **What is derived of a file where its texels are first read**: its key (`hashFile`), hashed once, and every
        /// fact above keyed from it, so a header carries what any later ask would read its file again for
        /// (`ImageDerivation`).
        FileFacts derive(const osg::Image& texels);

        /// `ChainPass`: an image's chain made again, or made where it carried none, read as its input says. False
        /// where the store held none and its run was not admitted (`ChainRuns`): `made` is then untouched.
        bool chain(const ChainPass::Input& input, MadeChain& made, ContentAsk ask = ContentAsk::Output);

        /// `ConversionPass`: a classic pair's base colour or metal-roughness map, as `chain` answers.
        bool convert(const ConversionPass::Input& input, MadeChain& made, ContentAsk ask = ContentAsk::Output);

        /// **`chain` as the device is handed it, into a buffer of `pool`**: read from the store's entry a segment at a
        /// time where it holds one that fits `input`, every level's digest checked, and made and copied in otherwise,
        /// so a hand-over allocates nothing once the pool is warm (`docs/architecture.md`, "A texture's levels are read
        /// by position, into a buffer the reader owns"). False where its run is not admitted, or `stop` asked while the
        /// pool held its most in use.
        bool chain(const ChainPass::Input& input, BufferPool& pool, const std::stop_token& stop, PooledChain& made);

        /// `convert` as the device is handed it, as `chain` into a pool answers.
        bool convert(
            const ConversionPass::Input& input, BufferPool& pool, const std::stop_token& stop, PooledChain& made);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

        /// Where this thread's asks for a header's texels are counted, which the next take carries (`readTexels`).
        TexelReads& texelReads() { return mStats.mTexelReads; }

        /// Appends to `into`, until it is handed null, the key of every entry an ask of a stored pass makes
        /// or finds: what a preparation files a model's walk under, so a later start asks the store for
        /// each (`PreparationManifest`). Null, as made, for none.
        void collectKeys(std::vector<StoredKey>* into) { mKeysInto = into; }

        /// What its chain and conversion buffers keep, the shared build's among them: every buffer of a
        /// `ChainScratch` but the made chain, which the caller holds, and the payload, which the store frees.
        ChainScratch chainScratch() const;

        /// Gives back the room its chain and conversion buffers keep, the build's among them.
        void letGoChainScratch();

        /// Gives back the room the texture passes read a file's finest level through.
        void letGoTexelScratch();

    private:
        /// Asks `pass` through the cache, and answers whether `output` holds what was asked: false only where its run
        /// was not admitted (`admits`).
        template <ContentPass Pass>
        bool run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output,
            ContentAsk ask = ContentAsk::Output);

        /// `run` of a chain or a conversion into a buffer of `pool` (`chain`'s pooled form).
        template <ContentPass Pass>
        bool runPooled(Pass& pass, const typename Pass::Input& input, BufferPool& pool, const std::stop_token& stop,
            PooledChain& made);

        /// The stored entry under `key` read into a buffer of `pool`, where it stands and fits `input`: its last
        /// segment the form's byte, the rest its levels. False where none stands, a segment does not read, or what
        /// reads does not fit, which is discarded.
        template <ContentPass Pass>
        bool readPooled(const ContentStore::Key& key, const typename Pass::Input& input, BufferPool& pool,
            const std::stop_token& stop, PooledChain& made);

        /// Whether `pass`'s run of `input` is made: a chain or a conversion as `mChainRuns` says, every other pass.
        template <ContentPass Pass>
        bool admits(const Pass& pass, const typename Pass::Input& input) const;

        ContentStats mStats;
        ShapePass mShape;
        TexelScratch mTexels;
        FileKeys mKeys{ mTexels };
        SolidReach mSolid{ mTexels, mKeys };
        TexelMean mMean{ mTexels, mKeys };
        SolidFall mFall{ mTexels, mKeys };
        AlphaSpan mSpan{ mTexels, mKeys };
        SpecularKind mKind{ mTexels, mKeys };
        ChainBuild mBuild;
        ChainPass mChain{ mBuild, mKeys, mStats.mTexelReads };
        ConversionPass mConversion{ mBuild, mKeys, mStats.mTexelReads };

        ContentCache mCache;
        ChainRuns mChainRuns = ChainRuns::Always;

        /// What a pooled run makes before it is copied into its buffer, the build's chain kept for the next.
        MadeChain mMade;
        std::vector<StoredKey>* mKeysInto = nullptr;
    };
}
