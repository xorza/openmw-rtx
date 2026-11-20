#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
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
#include <components/rtx/preprocess/texture/filefactspass.hpp>

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
    /// Each of `Pass`'s versions at its id, nought at an id none of them has.
    template <ContentPass... Pass>
    constexpr std::array<std::uint32_t, sContentPassCount> contentPassVersionsOf()
    {
        std::array<std::uint32_t, sContentPassCount> versions{};
        ((versions[static_cast<std::size_t>(Pass::sPass)] = Pass::sVersion), ...);
        return versions;
    }

    /// **Every pass's version, by its id**: what stands for the passes as a whole wherever all of them decide an
    /// answer, as a preparation's signature does (`ContentPreparation::signatureOf`). A pass with no entry here does
    /// not compile, where a list written out at its reader left one out.
    inline constexpr std::array<std::uint32_t, sContentPassCount> sContentPassVersions
        = contentPassVersionsOf<ShapePass, FileFactsPass, ChainPass, ConversionPass>();

    static_assert(std::ranges::none_of(sContentPassVersions, [](const std::uint32_t version) { return version == 0; }),
        "a pass whose version nothing states: every `ContentPassId` is in `sContentPassVersions`");

    /// Each store kind's version (`ContentStore::Policy::mVersions`): its pass's, and nought for a kind retired,
    /// which no entry was written at.
    inline constexpr std::array<std::uint32_t, sStoreKindCount> sStoreKindVersions = [] {
        std::array<std::uint32_t, sStoreKindCount> versions{};
        for (std::size_t pass = 0; pass < sContentPassCount; ++pass)
            versions[sStoreKinds[pass]] = sContentPassVersions[pass];
        return versions;
    }();

    /// The ray tracer's store: a kind a pass, each at its version, and nothing held in memory: a geometry carries its
    /// fold (`ModelFold`) and a header its facts, so the frame asks the store for nothing.
    inline constexpr ContentStore::Policy sContentStorePolicy{
        .mKindCount = sStoreKindCount,
        .mVersions = sStoreKindVersions,
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

        /// The first level `mBytes` holds, the finer ones left out (`MadeLayout::mFirst`).
        std::uint8_t mFirst = 0;
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
        explicit ContentPreprocessor(ContentStore::Store* store = nullptr, ChainRuns runs = ChainRuns::Always);

        ContentPreprocessor(const ContentPreprocessor&) = delete;
        ContentPreprocessor& operator=(const ContentPreprocessor&) = delete;
        ~ContentPreprocessor() = default;

        /// `ShapePass`: what a drawable's triangles come to, and its normals where they split.
        void shape(const ShapePass::Input& input, ShapePass::Output& output);

        /// `FileFactsPass`: every fact a material asks of `image`, which holds its own texels (`FileKeys::of`).
        ImageFacts facts(const osg::Image& image);

        /// **What is derived of a file where its texels are first read**: its key (`hashFile`), hashed once, and its
        /// facts keyed from it, so a header carries what any later ask would read its file again for
        /// (`ImageDerivation`).
        FileFacts derive(const osg::Image& texels);

        /// **`ChainPass`, held by the store and read by nothing**: a preparation's ask, which reads no entry the store
        /// holds and hands the store what a run made. False where the store held none and its run was not admitted
        /// (`ChainRuns`).
        bool prepareChain(const ChainPass::Input& input);

        /// `ConversionPass`, as `prepareChain` answers: a classic pair's base colour or metal-roughness map.
        bool prepareConversion(const ConversionPass::Input& input);

        /// **A chain as the device is handed it, from level `first` down, into a buffer of `pool`**: read from the
        /// store's entry a segment a level, from the first asked, where it holds one that fits `input`, every level's
        /// digest checked, and made and copied in otherwise, so a hand-over allocates nothing once the pool is warm
        /// and reads none of a level finer than its plan (`docs/architecture.md`, "A texture's levels are read by
        /// position, into a buffer the reader owns"). False where its run is not admitted, or `stop` asked while the
        /// pool held its most in use.
        bool chain(const ChainPass::Input& input, std::uint8_t first, BufferPool& pool, const std::stop_token& stop,
            PooledChain& made);

        /// `ConversionPass` as the device is handed it, as `chain` answers.
        bool convert(const ConversionPass::Input& input, std::uint8_t first, BufferPool& pool,
            const std::stop_token& stop, PooledChain& made);

        /// What every pass cost since the last take, and nothing counted from here on.
        ContentStats takeStats();

        /// Where this thread's asks for a header's texels are counted, which the next take carries (`readTexels`).
        TexelReads& texelReads() { return mStats.mTexelReads; }

        /// Appends to `into`, until it is handed null, the key of every entry an ask of a stored pass makes
        /// or finds: what a preparation files a model's walk under, so a later start asks the store for
        /// each (`PreparationManifest`). Null, as made, for none.
        void collectKeys(std::vector<StoredKey>* into) { mKeysInto = into; }

        /// What its chain and conversion buffers keep, the shared build's and the made chain's among them: every buffer
        /// of a `ChainScratch` but the payload, which the store frees.
        ChainScratch chainScratch() const;

        /// Gives back the room its chain and conversion buffers keep, the build's among them.
        void letGoChainScratch();

        /// Gives back the room the texture passes read a file's finest level through.
        void letGoTexelScratch();

    private:
        /// **The one ask of a pass through the cache**: `input` keyed, `found` asked of the key, and where it answers
        /// nothing and the run is admitted (`admits`), `make` run with the key, where there is a store, to make, keep
        /// and hand out what was asked. Answers whether what was asked stands: false only where its run was not
        /// admitted, or `make` could not hand it out. Each step timed into the pass's stats.
        template <ContentPass Pass, class Found, class Make>
        bool runCached(Pass& pass, const typename Pass::Input& input, Found&& found, Make&& make);

        /// A fact's or a shape's ask: `output` from the store's entry, or the pass run and offered to the store.
        template <ContentPass Pass>
        bool run(Pass& pass, const typename Pass::Input& input, typename Pass::Output& output);

        /// A chain's or a conversion's ask held by the store and read by nothing (`prepareChain`).
        template <ContentPass Pass>
        bool runStored(Pass& pass, const typename Pass::Input& input);

        /// A chain's or a conversion's ask into a buffer of `pool` (`chain`'s pooled form).
        template <ContentPass Pass>
        bool runPooled(Pass& pass, const typename Pass::Input& input, std::uint8_t first, BufferPool& pool,
            const std::stop_token& stop, PooledChain& made);

        /// The stored entry under `key` read into a buffer of `pool`, where it stands and fits `input`: its last
        /// segment the form's byte, the rest its levels. False where none stands, a segment does not read, or what
        /// reads does not fit, which is discarded.
        template <ContentPass Pass>
        bool readPooled(const ContentStore::Key& key, const typename Pass::Input& input, std::uint8_t first,
            BufferPool& pool, const std::stop_token& stop, PooledChain& made);

        /// The file key of `texels` a fact's ask is keyed by (`FileKeys::of`), timed as the facts' key.
        ContentStore::Key keyOf(const osg::Image& texels);

        /// Whether `pass`'s run of `input` is made: a chain or a conversion as `mChainRuns` says, every other pass.
        template <ContentPass Pass>
        bool admits(const Pass& pass, const typename Pass::Input& input) const;

        ContentStats mStats;
        ShapePass mShape;
        TexelScratch mTexels;
        FileKeys mKeys{ mTexels, mStats.mFileKeys };
        FileFactsPass mFacts{ mTexels };
        ChainBuild mBuild;
        ChainPass mChain{ mBuild, mKeys, mStats.mTexelReads };
        ConversionPass mConversion{ mBuild, mKeys, mStats.mTexelReads };

        ContentCache mCache;
        ChainRuns mChainRuns = ChainRuns::Always;

        /// What a chain's run makes before it is copied into its buffer or moved into the store, kept for the next.
        MadeChain mMade;
        std::vector<StoredKey>* mKeysInto = nullptr;
    };
}
