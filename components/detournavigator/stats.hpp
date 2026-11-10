#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_STATS_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_STATS_H

#include <cstddef>
#include <optional>

namespace Misc
{
    class FrameStats;
}

namespace DetourNavigator
{
    struct JobQueueStats
    {
        std::size_t mRemoving = 0;
        std::size_t mUpdating = 0;
        std::size_t mDelayed = 0;
    };

    struct TileStoreStats
    {
        /// Jobs queued for the store's reader, and reads it made.
        std::size_t mReading = 0;
        std::size_t mReads = 0;
    };

    struct NavMeshTilesCacheStats
    {
        std::size_t mNavMeshCacheSize = 0;
        std::size_t mUsedNavMeshTiles = 0;
        std::size_t mCachedNavMeshTiles = 0;
        std::size_t mHitCount = 0;
        std::size_t mGetCount = 0;
    };

    struct AsyncNavMeshUpdaterStats
    {
        std::size_t mJobs = 0;
        JobQueueStats mWaiting;
        std::size_t mPushed = 0;
        std::size_t mProcessing = 0;
        std::size_t mStoreHits = 0;
        std::size_t mStoreWrites = 0;
        std::size_t mPosted = 0;
        std::optional<TileStoreStats> mStore;
        NavMeshTilesCacheStats mCache;
    };

    struct TileCachedRecastMeshManagerStats
    {
        std::size_t mTiles = 0;
        std::size_t mObjects = 0;
        std::size_t mHeightfields = 0;
        std::size_t mWater = 0;
    };

    struct Stats
    {
        AsyncNavMeshUpdaterStats mUpdater;
        TileCachedRecastMeshManagerStats mRecast;
    };

    void reportStats(const Stats& stats, unsigned int frameNumber, Misc::FrameStats& out);
}

#endif
