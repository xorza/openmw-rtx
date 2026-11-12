#include "asyncnavmeshupdater.hpp"
#include "debug.hpp"
#include "makenavmesh.hpp"
#include "navmeshtilestore.hpp"
#include "settings.hpp"
#include "version.hpp"

#include <components/debug/debuglog.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/misc/thread.hpp>

#include <DetourNavMesh.h>

#include <osg/io_utils>

#include <boost/geometry.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <tuple>
#include <type_traits>

namespace DetourNavigator
{
    namespace
    {
        int getManhattanDistance(const TilePosition& lhs, const TilePosition& rhs)
        {
            return std::abs(lhs.x() - rhs.x()) + std::abs(lhs.y() - rhs.y());
        }

        bool isAbsentTileTooClose(const TilePosition& position, int distance,
            const std::set<std::tuple<AgentBounds, TilePosition>>& pushedTiles,
            const std::set<std::tuple<AgentBounds, TilePosition>>& presentTiles,
            const Misc::ScopeGuarded<std::set<std::tuple<AgentBounds, TilePosition>>>& processingTiles)
        {
            const auto isAbsentAndCloserThan = [&](const std::tuple<AgentBounds, TilePosition>& v) {
                return presentTiles.find(v) == presentTiles.end()
                    && getManhattanDistance(position, std::get<1>(v)) < distance;
            };
            if (std::any_of(pushedTiles.begin(), pushedTiles.end(), isAbsentAndCloserThan))
                return true;
            if (const auto locked = processingTiles.lockConst();
                std::any_of(locked->begin(), locked->end(), isAbsentAndCloserThan))
                return true;
            return false;
        }

        auto getAgentAndTile(const Job& job) noexcept
        {
            return std::make_tuple(job.mAgentBounds, job.mChangedTile);
        }

        std::unique_ptr<TileStoreReader> makeStoreReader(
            AsyncNavMeshUpdater& updater, NavMeshTileStore* store, const Settings& settings)
        {
            if (store == nullptr)
                return nullptr;
            return std::make_unique<TileStoreReader>(updater, *store, settings.mRecast);
        }

        std::size_t getNextJobId()
        {
            static std::atomic_size_t nextJobId{ 1 };
            return nextJobId.fetch_add(1);
        }

        std::string makeRevision(const Version& version)
        {
            return std::format(".{}.{}", version.mGeneration, version.mRevision);
        }

        void writeDebugRecastMesh(
            const Settings& settings, const TilePosition& tilePosition, const RecastMesh& recastMesh)
        {
            if (!settings.mEnableWriteRecastMeshToFile)
                return;
            std::string revision;
            if (settings.mEnableRecastMeshFileNameRevision)
                revision = makeRevision(recastMesh.getVersion());
            writeToFile(recastMesh,
                std::format("{}{}.{}.", settings.mRecastMeshPathPrefix, tilePosition.x(), tilePosition.y()), revision,
                settings.mRecast);
        }

        void writeDebugNavMesh(
            const Settings& settings, const GuardedNavMeshCacheItem& navMeshCacheItem, const Version& version)
        {
            if (!settings.mEnableWriteNavMeshToFile)
                return;
            std::string revision;
            if (settings.mEnableNavMeshFileNameRevision)
                revision = makeRevision(version);
            writeToFile(navMeshCacheItem.lockConst()->getImpl(), settings.mNavMeshPathPrefix, revision);
        }
    }

    std::ostream& operator<<(std::ostream& stream, JobStatus value)
    {
        switch (value)
        {
            case JobStatus::Done:
                return stream << "JobStatus::Done";
            case JobStatus::Fail:
                return stream << "JobStatus::Fail";
            case JobStatus::MemoryCacheMiss:
                return stream << "JobStatus::MemoryCacheMiss";
        }
        return stream << "JobStatus::" << static_cast<std::underlying_type_t<JobStatus>>(value);
    }

    Job::Job(const AgentBounds& agentBounds, std::weak_ptr<GuardedNavMeshCacheItem> navMeshCacheItem,
        ESM::RefId worldspace, const TilePosition& changedTile, ChangeType changeType,
        std::chrono::steady_clock::time_point processTime)
        : mId(getNextJobId())
        , mAgentBounds(agentBounds)
        , mNavMeshCacheItem(std::move(navMeshCacheItem))
        , mWorldspace(worldspace)
        , mChangedTile(changedTile)
        , mProcessTime(processTime)
        , mChangeType(changeType)
    {
    }

    void SpatialJobQueue::clear()
    {
        mValues.clear();
        mIndex.clear();
        mSize = 0;
    }

    void SpatialJobQueue::push(JobIt job)
    {
        auto it = mValues.find(job->mChangedTile);

        if (it == mValues.end())
        {
            it = mValues.emplace_hint(it, job->mChangedTile, std::deque<JobIt>());
            mIndex.insert(IndexValue(IndexPoint(job->mChangedTile.x(), job->mChangedTile.y()), it));
        }

        it->second.push_back(job);

        ++mSize;
    }

    std::optional<JobIt> SpatialJobQueue::pop(TilePosition playerTile)
    {
        const IndexPoint point(playerTile.x(), playerTile.y());
        const auto it = mIndex.qbegin(boost::geometry::index::nearest(point, 1));

        if (it == mIndex.qend())
            return std::nullopt;

        const UpdatingMap::iterator mapIt = it->second;
        std::deque<JobIt>& tileJobs = mapIt->second;
        JobIt result = tileJobs.front();
        tileJobs.pop_front();

        --mSize;

        if (tileJobs.empty())
        {
            mValues.erase(mapIt);
            mIndex.remove(*it);
        }

        return result;
    }

    void SpatialJobQueue::update(TilePosition playerTile, int maxTiles, std::vector<JobIt>& removing)
    {
        for (auto it = mValues.begin(); it != mValues.end();)
        {
            if (shouldAddTile(it->first, playerTile, maxTiles))
            {
                ++it;
                continue;
            }

            for (JobIt job : it->second)
            {
                job->mChangeType = ChangeType::remove;
                removing.push_back(job);
            }

            mSize -= it->second.size();
            mIndex.remove(IndexValue(IndexPoint(it->first.x(), it->first.y()), it));
            it = mValues.erase(it);
        }
    }

    bool JobQueue::hasJob(std::chrono::steady_clock::time_point now) const
    {
        return !mRemoving.empty() || mUpdating.size() > 0
            || (!mDelayed.empty() && mDelayed.front()->mProcessTime <= now);
    }

    void JobQueue::clear()
    {
        mRemoving.clear();
        mDelayed.clear();
        mUpdating.clear();
    }

    void JobQueue::push(JobIt job, std::chrono::steady_clock::time_point now)
    {
        if (job->mProcessTime > now)
        {
            mDelayed.push_back(job);
            return;
        }

        if (job->mChangeType == ChangeType::remove)
        {
            mRemoving.push_back(job);
            return;
        }

        mUpdating.push(job);
    }

    std::optional<JobIt> JobQueue::pop(TilePosition playerTile, std::chrono::steady_clock::time_point now)
    {
        if (!mRemoving.empty())
        {
            const JobIt result = mRemoving.back();
            mRemoving.pop_back();
            return result;
        }

        // A delayed job that is due competes with every other update by its distance to the player. Taken only
        // once no other update was queued, a tile near the player changed twice within the update interval
        // waited behind every farther tile, and on a slow machine a new game queues the whole navmesh.
        while (!mDelayed.empty() && mDelayed.front()->mProcessTime <= now)
        {
            mUpdating.push(mDelayed.front());
            mDelayed.pop_front();
        }

        return mUpdating.pop(playerTile);
    }

    void JobQueue::update(TilePosition playerTile, int maxTiles, std::chrono::steady_clock::time_point now)
    {
        mUpdating.update(playerTile, maxTiles, mRemoving);

        while (!mDelayed.empty() && mDelayed.front()->mProcessTime <= now)
        {
            const JobIt job = mDelayed.front();
            mDelayed.pop_front();

            if (shouldAddTile(job->mChangedTile, playerTile, maxTiles))
            {
                mUpdating.push(job);
            }
            else
            {
                job->mChangeType = ChangeType::remove;
                mRemoving.push_back(job);
            }
        }
    }

    AsyncNavMeshUpdater::AsyncNavMeshUpdater(const Settings& settings, TileCachedRecastMeshManager& recastMeshManager,
        OffMeshConnectionsManager& offMeshConnectionsManager, std::unique_ptr<NavMeshTileStore>&& store,
        const Clock& clock)
        : mSettings(settings)
        , mRecastMeshManager(recastMeshManager)
        , mOffMeshConnectionsManager(offMeshConnectionsManager)
        , mClock(clock)
        , mShouldStop()
        , mNavMeshTilesCache(settings.mMaxNavMeshTilesCacheSize)
        , mStore(std::move(store))
        , mStoreReader(makeStoreReader(*this, mStore.get(), mSettings))
    {
        for (std::size_t i = 0; i < mSettings.get().mAsyncNavMeshUpdaterThreads; ++i)
            mThreads.emplace_back([&] { process(); });
    }

    AsyncNavMeshUpdater::~AsyncNavMeshUpdater()
    {
        stop();
    }

    void AsyncNavMeshUpdater::post(const AgentBounds& agentBounds, const SharedNavMeshCacheItem& navMeshCacheItem,
        const TilePosition& playerTile, ESM::RefId worldspace, const std::map<TilePosition, ChangeType>& changedTiles)
    {
        bool playerTileChanged = false;
        {
            auto locked = mPlayerTile.lock();
            playerTileChanged = *locked != playerTile;
            *locked = playerTile;
        }

        if (!playerTileChanged && changedTiles.empty())
            return;

        std::unique_lock lock(mMutex);
        const std::chrono::steady_clock::time_point now = mClock.get().now();

        if (playerTileChanged)
        {
            Log(Debug::Debug) << "Player tile has been changed to " << playerTile;
            mWaiting.update(playerTile, mSettings.get().mMaxTilesNumber, now);
        }

        for (const auto& [changedTile, changeType] : changedTiles)
        {
            if (mPushed.emplace(agentBounds, changedTile).second)
            {
                const std::chrono::steady_clock::time_point processTime = [&] {
                    if (changeType != ChangeType::update)
                        return std::chrono::steady_clock::time_point();
                    const auto lastUpdate = mLastUpdates.find(std::tie(agentBounds, changedTile));
                    if (lastUpdate == mLastUpdates.end())
                        return std::chrono::steady_clock::time_point();
                    return lastUpdate->second + mSettings.get().mMinUpdateInterval;
                }();

                const JobIt it = mJobs.emplace(
                    mJobs.end(), agentBounds, navMeshCacheItem, worldspace, changedTile, changeType, processTime);

                Log(Debug::Debug) << "Post job " << it->mId << " for agent=(" << it->mAgentBounds << ")"
                                  << " changedTile=(" << it->mChangedTile << ")"
                                  << " changeType=" << it->mChangeType;

                mWaiting.push(it, now);
                ++mPostedCount;
            }
        }

        Log(Debug::Debug) << "Posted " << mJobs.size() << " navigator jobs";

        if (mWaiting.hasJob(now))
            mHasJob.notify_all();

        lock.unlock();

        if (playerTileChanged && mStoreReader != nullptr)
            mStoreReader->update(playerTile);
    }

    void AsyncNavMeshUpdater::wait(WaitConditionType waitConditionType, Loading::Listener* listener)
    {
        switch (waitConditionType)
        {
            case WaitConditionType::requiredTilesPresent:
                waitUntilJobsDoneForNotPresentTiles(listener);
                break;
            case WaitConditionType::allJobsDone:
                waitUntilAllJobsDone();
                break;
        }
    }

    void AsyncNavMeshUpdater::stop()
    {
        mShouldStop = true;
        if (mStoreReader != nullptr)
            mStoreReader->stop();
        std::unique_lock<std::mutex> lock(mMutex);
        mWaiting.clear();
        mHasJob.notify_all();
        lock.unlock();
        for (auto& thread : mThreads)
            if (thread.joinable())
                thread.join();
    }

    void AsyncNavMeshUpdater::waitUntilJobsDoneForNotPresentTiles(Loading::Listener* listener)
    {
        const int maxDistanceToPlayer = mSettings.get().mWaitUntilMinDistanceToPlayer;
        if (maxDistanceToPlayer <= 0)
            return;
        const std::size_t initialJobsLeft = getTotalJobs();
        std::size_t maxProgress = initialJobsLeft;
        std::size_t prevJobsLeft = initialJobsLeft;
        std::size_t jobsDone = 0;
        std::size_t jobsLeft = 0;
        const TilePosition playerPosition = *mPlayerTile.lockConst();
        const auto isDone = [&] {
            jobsLeft = mJobs.size();
            if (jobsLeft == 0)
                return true;
            return !isAbsentTileTooClose(playerPosition, maxDistanceToPlayer, mPushed, mPresentTiles, mProcessingTiles);
        };
        std::unique_lock<std::mutex> lock(mMutex);
        if (!isAbsentTileTooClose(playerPosition, maxDistanceToPlayer, mPushed, mPresentTiles, mProcessingTiles)
            || mJobs.empty())
            return;
        const Loading::ScopedLoad load(listener);
        if (listener != nullptr)
        {
            listener->setLabel("#{OMWEngine:BuildingNavigationMesh}");
            listener->setProgressRange(maxProgress);
        }
        while (!mDone.wait_for(lock, std::chrono::milliseconds(20), isDone))
        {
            if (listener == nullptr)
                continue;
            if (maxProgress < jobsLeft)
            {
                maxProgress = jobsLeft;
                listener->setProgressRange(maxProgress);
                listener->setProgress(jobsDone);
            }
            else if (jobsLeft < prevJobsLeft)
            {
                const std::size_t newJobsDone = prevJobsLeft - jobsLeft;
                jobsDone += newJobsDone;
                prevJobsLeft = jobsLeft;
                listener->increaseProgress(newJobsDone);
            }
        }
    }

    void AsyncNavMeshUpdater::waitUntilAllJobsDone()
    {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            mDone.wait(lock, [this] { return mJobs.empty(); });
        }
        mProcessingTiles.wait(mProcessed, [](const auto& v) { return v.empty(); });
    }

    AsyncNavMeshUpdaterStats AsyncNavMeshUpdater::getStats() const
    {
        AsyncNavMeshUpdaterStats result;
        {
            const std::lock_guard<std::mutex> lock(mMutex);
            result.mJobs = mJobs.size();
            result.mWaiting = mWaiting.getStats();
            result.mPushed = mPushed.size();
        }
        result.mProcessing = mProcessingTiles.lockConst()->size();
        if (mStoreReader != nullptr)
            result.mStore = mStoreReader->getStats();
        result.mCache = mNavMeshTilesCache.getStats();
        result.mStoreHits = mStoreHits.load(std::memory_order_relaxed);
        result.mStoreWrites = mStoreWrites.load(std::memory_order_relaxed);
        result.mPosted = mPostedCount.load(std::memory_order_relaxed);
        return result;
    }

    void AsyncNavMeshUpdater::process() noexcept
    {
        Log(Debug::Debug) << "Start process navigator jobs by thread=" << std::this_thread::get_id();
        Misc::setCurrentThreadIdlePriority();
        while (!mShouldStop)
        {
            if (JobIt job = getNextJob(); job != mJobs.end())
            {
                try
                {
                    const JobStatus status = processJob(*job);
                    Log(Debug::Debug) << "Processed job " << job->mId << " with status=" << status
                                      << " changeType=" << job->mChangeType;
                    switch (status)
                    {
                        case JobStatus::Done:
                            unlockTile(job->mId, job->mAgentBounds, job->mChangedTile);
                            // On the store's own thread and queue, which a write is handed to.
                            if (job->mGeneratedNavMeshData != nullptr)
                            {
                                mStore->write(*job->mKey, *job->mGeneratedNavMeshData);
                                mStoreWrites.fetch_add(1, std::memory_order_relaxed);
                            }
                            removeJob(job);
                            break;
                        case JobStatus::Fail:
                            unlockTile(job->mId, job->mAgentBounds, job->mChangedTile);
                            removeJob(job);
                            break;
                        case JobStatus::MemoryCacheMiss:
                        {
                            mStoreReader->enqueueJob(job);
                            break;
                        }
                    }
                }
                catch (const std::exception& e)
                {
                    Log(Debug::Warning) << "Failed to process navmesh job " << job->mId
                                        << " for worldspace=" << job->mWorldspace << " agent=" << job->mAgentBounds
                                        << " changedTile=(" << job->mChangedTile << ")"
                                        << " changeType=" << job->mChangeType
                                        << " by thread=" << std::this_thread::get_id() << ": " << e.what();
                    unlockTile(job->mId, job->mAgentBounds, job->mChangedTile);
                    removeJob(job);
                }
            }
            else
            {
                cleanupLastUpdates();
            }
        }
        Log(Debug::Debug) << "Stop navigator jobs processing by thread=" << std::this_thread::get_id();
    }

    JobStatus AsyncNavMeshUpdater::processJob(Job& job)
    {
        Log(Debug::Debug) << "Processing job " << job.mId << "  for worldspace=" << job.mWorldspace
                          << " agent=" << job.mAgentBounds << ""
                          << " changedTile=(" << job.mChangedTile << ")"
                          << " changeType=" << job.mChangeType << " by thread=" << std::this_thread::get_id();

        const auto navMeshCacheItem = job.mNavMeshCacheItem.lock();

        if (!navMeshCacheItem)
            return JobStatus::Done;

        const auto playerTile = *mPlayerTile.lockConst();

        if (!shouldAddTile(job.mChangedTile, playerTile, mSettings.get().mMaxTilesNumber))
        {
            Log(Debug::Debug) << "Ignore add tile by job " << job.mId << ": too far from player";
            job.mChangeType = ChangeType::remove;
            navMeshCacheItem->lock()->removeTile(job.mChangedTile);
            return JobStatus::Done;
        }

        switch (job.mState)
        {
            case JobState::Initial:
                return processInitialJob(job, *navMeshCacheItem);
            case JobState::WithStoreResult:
                return processJobWithStoreResult(job, *navMeshCacheItem);
        }

        return JobStatus::Done;
    }

    JobStatus AsyncNavMeshUpdater::markAsEmpty(const Job& job, GuardedNavMeshCacheItem& navMeshCacheItem)
    {
        {
            const std::scoped_lock lock(mMutex);
            mPresentTiles.erase(getAgentAndTile(job));
        }
        navMeshCacheItem.lock()->markAsEmpty(job.mChangedTile);
        return JobStatus::Done;
    }

    JobStatus AsyncNavMeshUpdater::processInitialJob(Job& job, GuardedNavMeshCacheItem& navMeshCacheItem)
    {
        Log(Debug::Debug) << "Processing initial job " << job.mId;

        std::shared_ptr<RecastMesh> recastMesh = mRecastMeshManager.get().getMesh(job.mWorldspace, job.mChangedTile);

        if (recastMesh == nullptr)
        {
            Log(Debug::Debug) << "Null recast mesh for job " << job.mId;
            return markAsEmpty(job, navMeshCacheItem);
        }

        if (isEmpty(*recastMesh))
        {
            Log(Debug::Debug) << "Empty bounds for job " << job.mId;
            return markAsEmpty(job, navMeshCacheItem);
        }

        try
        {
            writeDebugRecastMesh(mSettings, job.mChangedTile, *recastMesh);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to write debug recast mesh: " << e.what();
        }

        NavMeshTilesCache::Value cachedNavMeshData
            = mNavMeshTilesCache.get(job.mAgentBounds, job.mChangedTile, *recastMesh);
        std::unique_ptr<PreparedNavMeshData> preparedNavMeshData;
        const PreparedNavMeshData* preparedNavMeshDataPtr = nullptr;

        if (cachedNavMeshData)
        {
            preparedNavMeshDataPtr = &cachedNavMeshData.get();
        }
        else
        {
            if (job.mChangeType != ChangeType::update && mStoreReader != nullptr)
            {
                job.mRecastMesh = std::move(recastMesh);
                return JobStatus::MemoryCacheMiss;
            }

            preparedNavMeshData = prepareNavMeshTileData(
                *recastMesh, job.mWorldspace, job.mChangedTile, job.mAgentBounds, mSettings.get().mRecast);

            if (preparedNavMeshData == nullptr)
            {
                Log(Debug::Debug) << "Null navmesh data for job " << job.mId;
                return markAsEmpty(job, navMeshCacheItem);
            }

            if (job.mChangeType == ChangeType::update)
            {
                preparedNavMeshDataPtr = preparedNavMeshData.get();
            }
            else
            {
                cachedNavMeshData = mNavMeshTilesCache.set(
                    job.mAgentBounds, job.mChangedTile, *recastMesh, std::move(preparedNavMeshData));
                preparedNavMeshDataPtr = cachedNavMeshData ? &cachedNavMeshData.get() : preparedNavMeshData.get();
            }
        }

        const auto offMeshConnections = mOffMeshConnectionsManager.get().get(job.mChangedTile);

        assert(preparedNavMeshDataPtr != nullptr);

        const UpdateNavMeshStatus status
            = navMeshCacheItem.lock()->updateTile(job.mChangedTile, std::move(cachedNavMeshData),
                makeNavMeshTileData(*preparedNavMeshDataPtr, offMeshConnections, job.mAgentBounds, job.mChangedTile,
                    mSettings.get().mRecast));

        return handleUpdateNavMeshStatus(status, job, navMeshCacheItem, *recastMesh);
    }

    JobStatus AsyncNavMeshUpdater::processJobWithStoreResult(Job& job, GuardedNavMeshCacheItem& navMeshCacheItem)
    {
        Log(Debug::Debug) << "Processing job with store result " << job.mId;

        std::unique_ptr<PreparedNavMeshData> preparedNavMeshData = std::move(job.mStoredNavMeshData);
        bool generatedNavMeshData = false;

        if (preparedNavMeshData != nullptr)
            mStoreHits.fetch_add(1, std::memory_order_relaxed);

        if (preparedNavMeshData == nullptr)
        {
            preparedNavMeshData = prepareNavMeshTileData(
                *job.mRecastMesh, job.mWorldspace, job.mChangedTile, job.mAgentBounds, mSettings.get().mRecast);
            generatedNavMeshData = true;
        }

        if (preparedNavMeshData == nullptr)
        {
            Log(Debug::Debug) << "Null navmesh data for job " << job.mId;
            return markAsEmpty(job, navMeshCacheItem);
        }

        auto cachedNavMeshData = mNavMeshTilesCache.set(
            job.mAgentBounds, job.mChangedTile, *job.mRecastMesh, std::move(preparedNavMeshData));

        const auto offMeshConnections = mOffMeshConnectionsManager.get().get(job.mChangedTile);

        const PreparedNavMeshData* preparedNavMeshDataPtr
            = cachedNavMeshData ? &cachedNavMeshData.get() : preparedNavMeshData.get();
        assert(preparedNavMeshDataPtr != nullptr);

        const UpdateNavMeshStatus status
            = navMeshCacheItem.lock()->updateTile(job.mChangedTile, std::move(cachedNavMeshData),
                makeNavMeshTileData(*preparedNavMeshDataPtr, offMeshConnections, job.mAgentBounds, job.mChangedTile,
                    mSettings.get().mRecast));

        const JobStatus result = handleUpdateNavMeshStatus(status, job, navMeshCacheItem, *job.mRecastMesh);

        if (result == JobStatus::Done && job.mChangeType != ChangeType::update && job.mKey.has_value()
            && generatedNavMeshData)
            job.mGeneratedNavMeshData = std::make_unique<PreparedNavMeshData>(*preparedNavMeshDataPtr);

        return result;
    }

    JobStatus AsyncNavMeshUpdater::handleUpdateNavMeshStatus(UpdateNavMeshStatus status, const Job& job,
        const GuardedNavMeshCacheItem& navMeshCacheItem, const RecastMesh& recastMesh)
    {
        const Version navMeshVersion = navMeshCacheItem.lockConst()->getVersion();
        mRecastMeshManager.get().reportNavMeshChange(job.mChangedTile, recastMesh.getVersion(), navMeshVersion);

        if (status == UpdateNavMeshStatus::removed || status == UpdateNavMeshStatus::lost)
        {
            const std::scoped_lock lock(mMutex);
            mPresentTiles.erase(std::make_tuple(job.mAgentBounds, job.mChangedTile));
        }
        else if (isSuccess(status) && status != UpdateNavMeshStatus::ignored)
        {
            const std::scoped_lock lock(mMutex);
            mPresentTiles.insert(std::make_tuple(job.mAgentBounds, job.mChangedTile));
        }

        try
        {
            writeDebugNavMesh(mSettings, navMeshCacheItem, navMeshVersion);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Failed to write debug navmesh: " << e.what();
        }

        return isSuccess(status) ? JobStatus::Done : JobStatus::Fail;
    }

    JobIt AsyncNavMeshUpdater::getNextJob() noexcept
    {
        std::unique_lock<std::mutex> lock(mMutex);

        bool shouldStop = false;
        const auto hasJob = [&] {
            shouldStop = mShouldStop.load();
            return shouldStop || mWaiting.hasJob(mClock.get().now());
        };

        if (!mHasJob.wait_for(lock, std::chrono::milliseconds(10), hasJob))
        {
            if (mJobs.empty())
                mDone.notify_all();
            return mJobs.end();
        }

        if (shouldStop)
            return mJobs.end();

        const TilePosition playerTile = *mPlayerTile.lockConst();
        const std::chrono::steady_clock::time_point now = mClock.get().now();

        JobIt job = mJobs.end();

        if (const std::optional<JobIt> nextJob = mWaiting.pop(playerTile, now))
            job = *nextJob;

        if (job == mJobs.end())
            return job;

        Log(Debug::Debug) << "Pop job " << job->mId << " by thread=" << std::this_thread::get_id();

        if (job->mRecastMesh != nullptr)
            return job;

        if (!lockTile(job->mId, job->mAgentBounds, job->mChangedTile))
        {
            Log(Debug::Debug) << "Failed to lock tile by job " << job->mId;
            job->mProcessTime = now + mSettings.get().mMinUpdateInterval;
            mWaiting.push(job, now);
            return mJobs.end();
        }

        if (job->mChangeType == ChangeType::update)
            mLastUpdates[getAgentAndTile(*job)] = now;
        mPushed.erase(getAgentAndTile(*job));

        return job;
    }

    bool AsyncNavMeshUpdater::lockTile(
        std::size_t jobId, const AgentBounds& agentBounds, const TilePosition& changedTile)
    {
        Log(Debug::Debug) << "Locking tile by job " << jobId << " agent=" << agentBounds << " changedTile=("
                          << changedTile << ")";
        return mProcessingTiles.lock()->emplace(agentBounds, changedTile).second;
    }

    void AsyncNavMeshUpdater::unlockTile(
        std::size_t jobId, const AgentBounds& agentBounds, const TilePosition& changedTile)
    {
        auto locked = mProcessingTiles.lock();
        locked->erase(std::tie(agentBounds, changedTile));
        Log(Debug::Debug) << "Unlocked tile by job " << jobId << " agent=" << agentBounds << " changedTile=("
                          << changedTile << ")";
        if (locked->empty())
            mProcessed.notify_all();
    }

    std::size_t AsyncNavMeshUpdater::getTotalJobs() const
    {
        const std::scoped_lock lock(mMutex);
        return mJobs.size();
    }

    void AsyncNavMeshUpdater::cleanupLastUpdates() noexcept
    {
        const std::chrono::steady_clock::time_point now = mClock.get().now();

        const std::lock_guard<std::mutex> lock(mMutex);

        for (auto it = mLastUpdates.begin(); it != mLastUpdates.end();)
        {
            if (now - it->second > mSettings.get().mMinUpdateInterval)
                it = mLastUpdates.erase(it);
            else
                ++it;
        }
    }

    void AsyncNavMeshUpdater::enqueueJob(JobIt job)
    {
        Log(Debug::Debug) << "Enqueueing job " << job->mId << " by thread=" << std::this_thread::get_id();
        const std::lock_guard lock(mMutex);
        mWaiting.push(job, mClock.get().now());
        mHasJob.notify_all();
    }

    void AsyncNavMeshUpdater::removeJob(JobIt job)
    {
        Log(Debug::Debug) << "Removing job " << job->mId << " by thread=" << std::this_thread::get_id();
        const std::lock_guard lock(mMutex);
        mJobs.erase(job);
    }

    void TileStoreJobQueue::push(JobIt job)
    {
        const std::lock_guard lock(mMutex);
        mReading.push(job);
        mHasJob.notify_all();
    }

    std::optional<JobIt> TileStoreJobQueue::pop()
    {
        std::unique_lock lock(mMutex);
        mHasJob.wait(lock, [&] { return mShouldStop || mReading.size() > 0; });
        if (mShouldStop)
            return std::nullopt;
        return mReading.pop(mPlayerTile);
    }

    void TileStoreJobQueue::update(TilePosition playerTile)
    {
        const std::lock_guard lock(mMutex);
        mPlayerTile = playerTile;
    }

    void TileStoreJobQueue::stop()
    {
        const std::lock_guard lock(mMutex);
        mReading.clear();
        mShouldStop = true;
        mHasJob.notify_all();
    }

    std::size_t TileStoreJobQueue::size() const
    {
        const std::lock_guard lock(mMutex);
        return mReading.size();
    }

    TileStoreReader::TileStoreReader(
        AsyncNavMeshUpdater& updater, NavMeshTileStore& store, const RecastSettings& recastSettings)
        : mUpdater(updater)
        , mRecastSettings(recastSettings)
        , mStore(store)
        , mThread([this] { run(); })
    {
    }

    TileStoreReader::~TileStoreReader()
    {
        stop();
    }

    void TileStoreReader::enqueueJob(JobIt job)
    {
        Log(Debug::Debug) << "Enqueueing store job " << job->mId << " by thread=" << std::this_thread::get_id();
        mQueue.push(job);
    }

    TileStoreStats TileStoreReader::getStats() const
    {
        return TileStoreStats{
            .mReading = mQueue.size(),
            .mReads = mReads.load(std::memory_order_relaxed),
        };
    }

    void TileStoreReader::stop()
    {
        mShouldStop = true;
        mQueue.stop();
        if (mThread.joinable())
            mThread.join();
    }

    void TileStoreReader::run() noexcept
    {
        while (!mShouldStop)
        {
            if (const std::optional<JobIt> job = mQueue.pop())
            {
                read(**job);
                (*job)->mState = JobState::WithStoreResult;
                mUpdater.enqueueJob(*job);
            }
        }
    }

    void TileStoreReader::read(Job& job)
    {
        mReads.fetch_add(1, std::memory_order_relaxed);
        Log(Debug::Debug) << "Processing store read job " << job.mId;
        job.mKey = NavMeshTileStore::keyOf(
            mRecastSettings, job.mAgentBounds, job.mWorldspace, job.mChangedTile, *job.mRecastMesh);
        if (job.mKey.has_value())
            job.mStoredNavMeshData = mStore.read(*job.mKey);
    }
}
