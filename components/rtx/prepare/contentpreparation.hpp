#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <osg/Image>
#include <osg/Node>
#include <osg/ref_ptr>

#include <components/contentstore/key.hpp>
#include <components/platform/thread.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/image/texturewrap.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/shader/automaps.hpp>

#include "filestamps.hpp"
#include "preparationdispatcher.hpp"
#include "preparationmanifest.hpp"

namespace osg
{
    class Image;
}

namespace ContentStore
{
    class Store;
}

namespace Rtx
{
    class ContentSource;

    /// What a preparation reads and where it keeps what it computes.
    struct PreparationSetup
    {
        /// The models' templates and their images, as the cell reader reads them.
        ContentSource& mContent;

        /// The files' stamps, which the manifest is kept by.
        FileStamps& mStamps;

        ContentStore::Store& mStore;

        /// Where the manifest stands, beside the store.
        std::filesystem::path mManifest;

        /// Which nodes a model's walk descends into: the frame walk's own, so the walk reads what
        /// the game's reads.
        osg::Node::NodeMask mMask = ~0u;

        /// The companion maps the loader adds beside a diffuse, whose files a model's walk then
        /// depends on whether or not they stand: a map a mod adds is a model walked again.
        Shader::AutoMapRules mAutoMaps{};

        /// How many threads walk the models and make the chains.
        std::size_t mHands = 1;

        /// The bytes the jobs may hold together (`PreparationDispatcher`), or nothing for no bound, where
        /// the system states no memory (`Platform::availableMemory`).
        std::optional<std::uint64_t> mBudget{};

        /// Today, in days since the epoch in UTC: the day the manifest notes a model was named on, so
        /// a content list no run named for a month leaves it (`PreparationManifest::sUnnamedDaysMost`).
        std::uint32_t mDay = 0;
    };

    /// One ground layer's files, as the ground reader finds them (`ESMTerrain::Storage::getLayerInfo`),
    /// each a normalised path.
    struct PreparationLayer
    {
        /// The diffuse, or the specular map that stands for it.
        std::string mDiffuse;

        /// The normal map, or empty for none.
        std::string mNormal{};

        /// Whether the normal map may carry a height in its alpha, which the ground reads
        /// (`carriesHeight`).
        bool mParallax = false;

        /// Whether the diffuse is a `_diffusespec` the storage found, which `layerEncodingOf` reads.
        bool mDiffuseSpec = false;
    };

    /// What a preparation took, once `finish` returned.
    struct PreparationTimes
    {
        /// From the start to the last job done.
        std::chrono::nanoseconds mWall{};

        /// What its threads ran on a CPU, every one summed (`Platform::cpuTimeOfThisThread`).
        std::chrono::nanoseconds mThreadsCpu{};

        std::size_t mThreads = 0;

        /// The budget the jobs were dispatched within, if any, and the most they held of it, as counted.
        std::optional<std::uint64_t> mBudget{};
        std::uint64_t mHeldMost = 0;
    };

    /// The game's import step, run on its loading screen (`docs/rtx/architecture.md`, "The store is
    /// filled on the loading screen, in the game's own process"): every model the content names walked
    /// as the cell reader walks it, on threads of its own, so every pass that walk asks — the shapes
    /// and the texture facts — and every chain its maps need is in the store before play, and play
    /// reads it there; and every ground layer's maps, as the ground reader reads them. A model or a
    /// layer whose files stand as the last preparation found them is not read again
    /// (`PreparationManifest`). **A walk queues a chain job for each map it claims**, and the threads take
    /// walks and chains as a dispatcher hands them out within a budget of memory (`PreparationDispatcher`,
    /// `docs/rtx/architecture.md`, "The preparation's memory is dispatched"); the loader's caches let go of
    /// each file as the last job reading it ends.
    ///
    /// Starts when made. Pinned: the threads reach every member.
    class ContentPreparation
    {
    public:
        /// Starts walking `models`, each a normalised path, and reading `layers`, on `setup.mHands`
        /// threads.
        ContentPreparation(
            const PreparationSetup& setup, std::vector<std::string> models, std::vector<PreparationLayer> layers);

        /// Stops every thread, the models not yet walked left as they are.
        ~ContentPreparation();

        ContentPreparation(const ContentPreparation&) = delete;
        ContentPreparation& operator=(const ContentPreparation&) = delete;
        ContentPreparation(ContentPreparation&&) = delete;
        ContentPreparation& operator=(ContentPreparation&&) = delete;

        /// How many of the models and layers are done, read or found as they were, once they are all
        /// done or `patience` passed: a waiter that sleeps between the workers' reports, as
        /// `Renderer::awaitKernels` waits.
        JobProgress awaitProgress(std::chrono::milliseconds patience);

        /// Once every model is done: the threads joined and the manifest written for what the run
        /// read, where anything moved. Answers whether the manifest on disk is this run's.
        bool finish();

        /// What the threads computed and found, once `finish` returned.
        ContentStats getStats() const { return mStats; }

        /// How many models and layers were read, the rest found as they were, once `finish` returned.
        std::size_t getWalked() const { return mWalkedCount; }

        /// What it took, once `finish` returned.
        PreparationTimes getTimes() const { return mTimes; }

        /// What the preparation computes and how, which a manifest is kept under: a pass whose
        /// version moved, another mask or another layout is every model walked again.
        static std::uint64_t signatureOf(const PreparationSetup& setup);

    private:
        struct Hand;

        /// A chain a walk claimed and queued, holding its images until it is made: a conversion's partner
        /// beside its file, and the walk it is filed under (`mWalked`).
        struct QueuedChain
        {
            osg::ref_ptr<const osg::Image> mImage;
            osg::ref_ptr<const osg::Image> mPartner;
            TextureEncoding mEncoding;
            TextureWrap mWrap;
            std::size_t mWalk;
        };

        /// `serveJobs`, its CPU time taken around it.
        void serve(Hand& hand, const std::stop_token& stop);

        /// Takes what the dispatcher hands this thread and does it, until every job is done or a stop.
        void serveJobs(Hand& hand, const std::stop_token& stop);

        /// Walks job `at`, unless it stands as the last preparation found it, and queues the chains it
        /// claimed.
        void walkJob(Hand& hand, std::size_t at);

        /// Makes the queued chain `chain`.
        void makeChain(Hand& hand, std::size_t chain);

        void walk(Hand& hand, std::size_t at);
        void read(Hand& hand, const PreparationLayer& layer);

        /// A job's name in the manifest: a model's path, or a layer's files (`mLayerNames`).
        const std::string& nameOf(std::size_t at) const;

        /// `input`'s chain claimed for the walk where it is made at all and this run is the first to ask.
        void chain(Hand& hand, const ChainPass::Input& input);

        /// `input`'s converted chain claimed for the walk where this run is the first to ask.
        void convert(Hand& hand, const ConversionPass::Input& input);

        /// Whether this run is the first to ask for `name`'s chain as `encoding`: a map many models
        /// wear is made once a run.
        bool claim(std::string name);

        /// Counts a job's use of `path`, a model or an image, which the loader's caches let go of once its
        /// last use ends (`letGoOf`). Under the lock.
        void use(const std::string& path);

        /// Ends a use of `path`, and lists it in `letGo` where it was the last. Under the lock.
        void endUse(const std::string& path, std::vector<std::string>& letGo);

        /// Has the loader let go of every path in `paths`. Outside the lock.
        void letGoOf(std::vector<std::string>& paths);

        /// Seconds since the start, the dispatcher's clock.
        double secondsNow() const;

        /// Counts `image` among those the hand's walk met, once.
        static void meet(Hand& hand, const osg::Image& image);

        ContentSource& mContent;

        /// The setup's stamps, each file's asked once a run.
        KeptStamps mStamps;
        ContentStore::Store& mStore;
        const std::filesystem::path mManifestPath;
        const osg::Node::NodeMask mMask;
        const Shader::AutoMapRules mAutoMaps;
        const std::uint64_t mSignature;

        const std::vector<std::string> mModels;
        const std::vector<PreparationLayer> mLayers;

        /// Each layer's name in the manifest: its diffuse and its normal map, so a normal map a mod
        /// adds beside a diffuse is a layer read again though the diffuse stands as it was.
        const std::vector<std::string> mLayerNames;

        /// What the last preparations read, which `finish` files this run into: read by every thread
        /// while the models are walked, and written by `finish` alone, after them.
        PreparationManifest mManifest;

        /// The day this run names its models on (`PreparationSetup::mDay`).
        const std::uint32_t mDay;

        /// `PreparationSetup::mBudget`.
        const std::optional<std::uint64_t> mBudget;

        /// Each job's entry where this run read it, the models first and the layers after them,
        /// written by the one thread that read it.
        std::vector<std::optional<PreparationManifest::Entry>> mWalked;

        const std::chrono::steady_clock::time_point mStarted = std::chrono::steady_clock::now();

        /// What the threads share under it: which job each takes, the chains queued and claimed, the paths
        /// in use, how many walks are done and when the last job was, which a waiter is woken by.
        std::mutex mMutex;
        std::condition_variable mToHands;
        std::condition_variable mToWaiter;
        PreparationDispatcher mDispatcher;

        /// In a deque, whose elements stand where they are as it grows: a thread makes a chain out of the
        /// lock while a walk queues more.
        std::deque<QueuedChain> mChains;
        std::unordered_set<std::string> mClaimed;
        std::unordered_map<std::string, std::uint32_t> mUses;

        /// Each chain's keys and the walk they are filed under, which `finish` adds to the walks' entries.
        std::vector<std::pair<std::size_t, StoredKey>> mChainKeys;

        std::uint32_t mMade = 0;
        bool mFinished = false;
        std::chrono::steady_clock::time_point mDone = mStarted;

        ContentStats mStats;
        std::size_t mWalkedCount = 0;
        PreparationTimes mTimes;

        /// Each thread's walk and its thread, last, so the threads are joined before the members they read go.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
