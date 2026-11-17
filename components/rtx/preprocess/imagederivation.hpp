#pragma once

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

#include "contentpreprocessor.hpp"
#include "contentstats.hpp"

namespace ContentStore
{
    class Store;
}

namespace osg
{
    class Image;
}

namespace Resource
{
    class ImageHeader;
}

namespace Rtx
{
    /// **What the renderer derives of a file where its texels are first read** (`Resource::HeaderDeriver`): its key and
    /// every fact the materials ask (`ContentPreprocessor::derive`), made then by one of a few preprocessors and filed
    /// on the header (`HeaderDerived`), so no thread reads the file again to key it or learn a fact
    /// (`docs/architecture.md`, "A file is keyed once, where its texels are first read"). Each fact is asked through
    /// the store, which may hold it from a session before. Asked from every loader's thread at once.
    class ImageDerivation
    {
    public:
        /// @param store where the facts are found and kept, or null for none.
        /// @param threads how many preprocessors it keeps, the most threads that derive at once: one more waits.
        ImageDerivation(ContentStore::Store* store, std::size_t threads);

        ImageDerivation(const ImageDerivation&) = delete;
        ImageDerivation& operator=(const ImageDerivation&) = delete;
        ~ImageDerivation();

        /// Derives `texels`, `header`'s file's, and files it on `header`.
        void derive(const osg::Image& texels, const Resource::ImageHeader& header);

        /// What every derivation's passes cost since the last take.
        ContentStats takeStats();

    private:
        ContentStore::Store* const mStore;
        const std::size_t mThreads;

        std::mutex mMutex;
        std::condition_variable mFreed;
        std::vector<std::unique_ptr<ContentPreprocessor>> mFree;
        std::size_t mMade = 0;
        ContentStats mStats;
    };
}
