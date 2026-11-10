#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_ASYNCNAVMESHUPDATER_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_ASYNCNAVMESHUPDATER_H

#include "agentbounds.hpp"
#include "changetype.hpp"
#include "guardednavmeshcacheitem.hpp"
#include "navmeshcacheitem.hpp"
#include "navmeshtilescache.hpp"
#include "navmeshtilestore.hpp"
#include "offmeshconnectionsmanager.hpp"
#include "sharednavmeshcacheitem.hpp"
#include "stats.hpp"
#include "tilecachedrecastmeshmanager.hpp"
#include "tileposition.hpp"
#include "waitconditiontype.hpp"

#include <components/contentstore/key.hpp>

#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/index/rtree.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iosfwd>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <tuple>

class dtNavMesh;

namespace Loading
{
    class Listener;
}

namespace DetourNavigator
{
    enum class JobState
    {
        Initial,
        WithStoreResult,
    };

    struct Job
    {
        const std::size_t mId;
        const AgentBounds mAgentBounds;
        const std::weak_ptr<GuardedNavMeshCacheItem> mNavMeshCacheItem;
        const ESM::RefId mWorldspace;
        const TilePosition mChangedTile;
        std::chrono::steady_clock::time_point mProcessTime;
        ChangeType mChangeType;
        JobState mState = JobState::Initial;
        std::shared_ptr<RecastMesh> mRecastMesh;

        /// The key the store files the tile under, once the store's reader made it: nothing where a source has
        /// no file hash, and the tile is generated and not stored.
        std::optional<ContentStore::Key> mKey;
        std::unique_ptr<PreparedNavMeshData> mStoredNavMeshData;
        std::unique_ptr<PreparedNavMeshData> mGeneratedNavMeshData;

        Job(const AgentBounds& agentBounds, std::weak_ptr<GuardedNavMeshCacheItem> navMeshCacheItem,
            ESM::RefId worldspace, const TilePosition& changedTile, ChangeType changeType,
            std::chrono::steady_clock::time_point processTime);
    };

    using JobIt = std::list<Job>::iterator;

    class SpatialJobQueue
    {
    public:
        std::size_t size() const { return mSize; }

        void clear();

        void push(JobIt job);

        std::optional<JobIt> pop(TilePosition playerTile);

        void update(TilePosition playerTile, int maxTiles, std::vector<JobIt>& removing);

    private:
        using IndexPoint = boost::geometry::model::point<int, 2, boost::geometry::cs::cartesian>;
        using UpdatingMap = std::map<TilePosition, std::deque<JobIt>>;
        using IndexValue = std::pair<IndexPoint, UpdatingMap::iterator>;

        std::size_t mSize = 0;
        UpdatingMap mValues;
        boost::geometry::index::rtree<IndexValue, boost::geometry::index::linear<4>> mIndex;
    };

    class JobQueue
    {
    public:
        JobQueueStats getStats() const
        {
            return JobQueueStats{
                .mRemoving = mRemoving.size(),
                .mUpdating = mUpdating.size(),
                .mDelayed = mDelayed.size(),
            };
        }

        bool hasJob(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;

        void clear();

        void push(JobIt job, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

        std::optional<JobIt> pop(
            TilePosition playerTile, std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

        void update(TilePosition playerTile, int maxTiles,
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());

    private:
        std::vector<JobIt> mRemoving;
        SpatialJobQueue mUpdating;
        std::deque<JobIt> mDelayed;
    };

    enum class JobStatus
    {
        Done,
        Fail,
        MemoryCacheMiss,
    };

    std::ostream& operator<<(std::ostream& stream, JobStatus value);

    /// The jobs the store's reader is handed, nearest the player first.
    class TileStoreJobQueue
    {
    public:
        void push(JobIt job);

        std::optional<JobIt> pop();

        void update(TilePosition playerTile);

        void stop();

        std::size_t size() const;

    private:
        mutable std::mutex mMutex;
        std::condition_variable mHasJob;
        SpatialJobQueue mReading;
        TilePosition mPlayerTile;
        bool mShouldStop = false;
    };

    class AsyncNavMeshUpdater;

    /// Reads a job's tile from the store on a thread of its own, off the updater's, which a read would wait
    /// on the disk on, and hands the job back with what it found.
    class TileStoreReader
    {
    public:
        TileStoreReader(AsyncNavMeshUpdater& updater, NavMeshTileStore& store, const RecastSettings& recastSettings);

        ~TileStoreReader();

        TileStoreStats getStats() const;

        void enqueueJob(JobIt job);

        void update(TilePosition playerTile) { mQueue.update(playerTile); }

        void stop();

    private:
        AsyncNavMeshUpdater& mUpdater;
        const RecastSettings& mRecastSettings;
        NavMeshTileStore& mStore;
        TileStoreJobQueue mQueue;
        std::atomic_bool mShouldStop{ false };
        std::atomic_size_t mReads{ 0 };
        std::thread mThread;

        inline void run() noexcept;

        inline void read(Job& job);
    };

    class AsyncNavMeshUpdater
    {
    public:
        AsyncNavMeshUpdater(const Settings& settings, TileCachedRecastMeshManager& recastMeshManager,
            OffMeshConnectionsManager& offMeshConnectionsManager, std::unique_ptr<NavMeshTileStore>&& store);
        ~AsyncNavMeshUpdater();

        void post(const AgentBounds& agentBounds, const SharedNavMeshCacheItem& navMeshCacheItem,
            const TilePosition& playerTile, ESM::RefId worldspace,
            const std::map<TilePosition, ChangeType>& changedTiles);

        void wait(WaitConditionType waitConditionType, Loading::Listener* listener);

        void stop();

        AsyncNavMeshUpdaterStats getStats() const;

        void enqueueJob(JobIt job);

        void removeJob(JobIt job);

    private:
        std::reference_wrapper<const Settings> mSettings;
        std::reference_wrapper<TileCachedRecastMeshManager> mRecastMeshManager;
        std::reference_wrapper<OffMeshConnectionsManager> mOffMeshConnectionsManager;
        std::atomic_bool mShouldStop;
        mutable std::mutex mMutex;
        std::condition_variable mHasJob;
        std::condition_variable mDone;
        std::condition_variable mProcessed;
        std::list<Job> mJobs;
        JobQueue mWaiting;
        std::set<std::tuple<AgentBounds, TilePosition>> mPushed;
        Misc::ScopeGuarded<TilePosition> mPlayerTile;
        NavMeshTilesCache mNavMeshTilesCache;
        Misc::ScopeGuarded<std::set<std::tuple<AgentBounds, TilePosition>>> mProcessingTiles;
        std::map<std::tuple<AgentBounds, TilePosition>, std::chrono::steady_clock::time_point> mLastUpdates;
        std::set<std::tuple<AgentBounds, TilePosition>> mPresentTiles;
        std::vector<std::thread> mThreads;
        /// Before its reader, which reads it until it stops.
        std::unique_ptr<NavMeshTileStore> mStore;
        std::unique_ptr<TileStoreReader> mStoreReader;
        std::atomic_size_t mStoreHits{ 0 };
        std::atomic_size_t mStoreWrites{ 0 };
        std::atomic_size_t mPostedCount{ 0 };

        void process() noexcept;

        JobStatus processJob(Job& job);

        inline JobStatus markAsEmpty(const Job& job, GuardedNavMeshCacheItem& navMeshCacheItem);

        inline JobStatus processInitialJob(Job& job, GuardedNavMeshCacheItem& navMeshCacheItem);

        inline JobStatus processJobWithStoreResult(Job& job, GuardedNavMeshCacheItem& navMeshCacheItem);

        inline JobStatus handleUpdateNavMeshStatus(UpdateNavMeshStatus status, const Job& job,
            const GuardedNavMeshCacheItem& navMeshCacheItem, const RecastMesh& recastMesh);

        inline JobIt getNextJob() noexcept;

        void postThreadJob(JobIt job, std::deque<JobIt>& queue);

        bool lockTile(std::size_t jobId, const AgentBounds& agentBounds, const TilePosition& changedTile);

        void unlockTile(std::size_t jobId, const AgentBounds& agentBounds, const TilePosition& changedTile);

        inline std::size_t getTotalJobs() const;

        inline void cleanupLastUpdates() noexcept;

        inline void waitUntilJobsDoneForNotPresentTiles(Loading::Listener* listener);

        inline void waitUntilAllJobsDone();
    };
}

#endif
