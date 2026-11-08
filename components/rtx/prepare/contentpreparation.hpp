#pragma once

#include <atomic>
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
#include <components/rtx/scene/specularlayout.hpp>
#include <components/shader/automaps.hpp>

#include "preparationmanifest.hpp"

namespace osg
{
    class Image;
}

namespace Rtx
{
    class ContentSource;
    class ContentStore;
    class FileStamps;

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

        /// What a specular map's channels mean, which decides how its chain is made.
        SpecularLayout mSpecular = SpecularLayout::Classic;

        /// The companion maps the loader adds beside a diffuse, whose files a model's walk then
        /// depends on whether or not they stand: a map a mod adds is a model walked again.
        Shader::AutoMapRules mAutoMaps{};

        /// How many threads walk the models.
        std::size_t mHands = 1;
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

    /// The game's import step, run on its loading screen (`.notes/content-cache.md`, the store filled
    /// on the loading screen): every model the content names walked as the cell reader walks it, on
    /// threads of its own, so every pass that walk asks — the shapes and the texture facts — and
    /// every chain its maps need is in the store before play, and play reads it there; and every
    /// ground layer's maps, as the ground reader reads them. A model or a layer whose files stand as the
    /// last preparation found them is not read again (`PreparationManifest`).
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

        /// How many of the models and layers are done, read or found as they were.
        JobProgress getProgress() const;

        /// Once every model is done: the threads joined and the manifest written for what the run
        /// read, where anything moved. Answers whether the manifest on disk is this run's.
        bool finish();

        /// What the threads computed and found, once `finish` returned.
        ContentStats getStats() const { return mStats; }

        /// How many models and layers were read, the rest found as they were, once `finish` returned.
        std::size_t getWalked() const { return mWalkedCount; }

        /// What the preparation computes and how, which a manifest is kept under: a pass whose
        /// version moved, another mask or another layout is every model walked again.
        static std::uint64_t signatureOf(const PreparationSetup& setup);

    private:
        struct Hand;

        void serve(Hand& hand, const Platform::StopToken& stop);
        void walk(Hand& hand, std::size_t at);
        void read(Hand& hand, const PreparationLayer& layer);

        /// A job's name in the manifest: a model's path, or a layer's files (`mLayerNames`).
        const std::string& nameOf(std::size_t at) const;

        /// `input`'s chain made where it is made at all and this run is the first to ask.
        void chain(Hand& hand, const ChainPass::Input& input);

        /// Whether this run is the first to ask for `name`'s chain as `encoding`: a map many models
        /// wear is made once a run.
        bool claim(std::string name);

        ContentSource& mContent;
        FileStamps& mStamps;
        ContentStore& mStore;
        const std::filesystem::path mManifestPath;
        const osg::Node::NodeMask mMask;
        const SpecularLayout mSpecular;
        const Shader::AutoMapRules mAutoMaps;
        const std::uint64_t mSignature;

        const std::vector<std::string> mModels;
        const std::vector<PreparationLayer> mLayers;

        /// Each layer's name in the manifest: its diffuse and its normal map, so a normal map a mod
        /// adds beside a diffuse is a layer read again though the diffuse stands as it was.
        const std::vector<std::string> mLayerNames;

        const PreparationManifest mPrevious;

        /// Each job's entry where this run read it, the models first and the layers after them,
        /// written by the one thread that read it.
        std::vector<std::optional<PreparationManifest::Entry>> mWalked;

        std::atomic<std::size_t> mNext{ 0 };
        std::atomic<std::uint32_t> mMade{ 0 };

        Monitor mMonitor;
        std::unordered_set<std::string> mClaimed;

        ContentStats mStats;
        std::size_t mWalkedCount = 0;

        /// Each thread's walk and its worker, last, for the reason `Worker` gives.
        std::vector<std::unique_ptr<Hand>> mHands;
    };
}
