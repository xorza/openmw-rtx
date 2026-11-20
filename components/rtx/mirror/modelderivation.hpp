#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include <components/resource/scenemanager.hpp>
#include <components/rtx/preprocess/contentstats.hpp>

namespace ContentStore
{
    class Store;
}

namespace osg
{
    class Node;
}

namespace Rtx
{
    /// **What the renderer derives of a model where it is loaded** (`Resource::ModelDeriver`): each geometry's fold
    /// and split, found in the store or made there and filed, kept on the geometry (`ModelFold`), so the ring's reader
    /// and the frame's walk read a drawable's shape off it and ask the store nothing. Made by one of a few hands, each
    /// a preprocessor and a reader; a loader past them waits. Asked from every loader's thread at once.
    class ModelDerivation final : public Resource::ModelDeriver
    {
    public:
        /// @param store where the folds are found and kept, or null for none.
        /// @param threads how many hands it keeps, the most loaders that derive at once: one more waits.
        ModelDerivation(ContentStore::Store* store, std::size_t threads);

        ModelDerivation(const ModelDerivation&) = delete;
        ModelDerivation& operator=(const ModelDerivation&) = delete;
        ~ModelDerivation() override;

        /// Folds every geometry of `model` that carries no fold yet.
        void derive(osg::Node& model) override;

        /// What every derivation's passes cost since the last take.
        ContentStats takeStats();

    private:
        struct Hand;

        ContentStore::Store* const mStore;
        const std::size_t mThreads;

        std::mutex mMutex;
        std::condition_variable mFreed;
        std::vector<std::unique_ptr<Hand>> mFree;
        std::size_t mMade = 0;
        ContentStats mStats;
    };
}
