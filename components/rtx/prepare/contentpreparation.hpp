#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <osg/Node>

#include <components/platform/thread.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/common/monitor.hpp>
#include <components/rtx/image/textureencoding.hpp>
#include <components/rtx/preprocess/contentstats.hpp>
#include <components/rtx/preprocess/texture/chainpass.hpp>
#include <components/rtx/preprocess/texture/conversionpass.hpp>
#include <components/shader/automaps.hpp>

#include "filestamps.hpp"
#include "preparationmanifest.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    class ContentSource;
    class ContentStore;

    /// What a preparation reads and where it keeps what it computes.
    struct PreparationSetup
    {
        /// The models' templates and their images, as the cell reader reads them.
        ContentSource& mContent;

        /// The files' stamps, which the manifest is kept by.
        FileStamps& mStamps;

        ContentStore& mStore;

        /// Where the manifest stands, beside the store.
        std::filesystem::path mManifest;

        /// Which nodes a model's walk descends into: the frame walk's own, so the walk reads what
        /// the game's reads.
        osg::Node::NodeMask mMask = ~0u;

        /// The companion maps the loader adds beside a diffuse, whose files a model's walk then
        /// depends on whether or not they stand: a map a mod adds is a model walked again.
        Shader::AutoMapRules mAutoMaps{};

        /// How many threads walk the models.
        std::size_t mHands = 1;

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
    };

    /// The game's import step, run on its loading screen (`docs/rtx/architecture.md`, "The store is
    /// filled on the loading screen, in the game's own process"): every model the content names walked
    /// as the cell reader walks it, on threads of its own, so every pass that walk asks — the shapes
    /// and the texture facts — and every chain its maps need is in the store before play, and play
    /// reads it there; and every ground layer's maps, as the ground reader reads them. A model or a
    /// layer whose files stand as the last preparation found them is not read again
    /// (`PreparationManifest`).
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

        /// `serveJobs`, its CPU time taken around it.
        void serve(Hand& hand, const Platform::StopToken& stop);
        void serveJobs(Hand& hand, const Platform::StopToken& stop);
        void walk(Hand& hand, std::size_t at);
        void read(Hand& hand, const PreparationLayer& layer);

        /// A job's name in the manifest: a model's path, or a layer's files (`mLayerNames`).
        const std::string& nameOf(std::size_t at) const;

        /// `input`'s chain made where it is made at all and this run is the first to ask.
        void chain(Hand& hand, const ChainPass::Input& input);

        /// `input`'s converted chain made where this run is the first to ask.
        void convert(Hand& hand, const ConversionPass::Input& input);

        /// Whether this run is the first to ask for `name`'s chain as `encoding`: a map many models
        /// wear is made once a run.
        bool claim(std::string name);

        ContentSource& mContent;

        /// The setup's stamps, each file's asked once a run.
        KeptStamps mStamps;
        ContentStore& mStore;
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

        /// Each job's entry where this run read it, the models first and the layers after them,
        /// written by the one thread that read it.
        std::vector<std::optional<PreparationManifest::Entry>> mWalked;

        /// The next job to serve, in serving order: the layers, then the models (`serveJobs`).
        std::atomic<std::size_t> mNext{ 0 };

        const std::chrono::steady_clock::time_point mStarted = std::chrono::steady_clock::now();

        /// What the threads share under it: how many jobs are done, which a waiter is woken by, when
        /// the last was, and the chains claimed.
        Monitor mMonitor;
        std::uint32_t mMade = 0;
        std::chrono::steady_clock::time_point mDone = mStarted;
        std::unordered_set<std::string> mClaimed;

        ContentStats mStats;
        std::size_t mWalkedCount = 0;
        PreparationTimes mTimes;

        /// Each thread's walk and its worker, last, for the reason `Worker` gives.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
