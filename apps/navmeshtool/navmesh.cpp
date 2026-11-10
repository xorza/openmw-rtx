#include "navmesh.hpp"

#include "worldspacedata.hpp"

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/debug/debugging.hpp>
#include <components/debug/debuglog.hpp>
#include <components/detournavigator/generatenavmeshtile.hpp>
#include <components/detournavigator/navmeshtilestore.hpp>
#include <components/detournavigator/preparednavmeshdata.hpp>
#include <components/detournavigator/recastmesh.hpp>
#include <components/detournavigator/recastmeshprovider.hpp>
#include <components/detournavigator/settings.hpp>
#include <components/detournavigator/tileposition.hpp>
#include <components/misc/progressreporter.hpp>
#include <components/navmeshtool/protocol.hpp>
#include <components/sceneutil/workqueue.hpp>

#include <osg/Vec3f>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NavMeshTool
{
    namespace
    {
        using DetourNavigator::AgentBounds;
        using DetourNavigator::GenerateNavMeshTile;
        using DetourNavigator::NavMeshTileConsumerStats;
        using DetourNavigator::NavMeshTileStore;
        using DetourNavigator::PreparedNavMeshData;
        using DetourNavigator::RecastMesh;
        using DetourNavigator::Settings;
        using DetourNavigator::TilePosition;

        // Limits pending tiles between worldspaces.
        constexpr std::size_t maxTilesInFlight = 4096;

        void logGeneratedTiles(std::size_t provided, std::size_t expected)
        {
            Log(Debug::Info) << provided << "/" << expected << " ("
                             << (static_cast<double>(provided) / static_cast<double>(expected) * 100)
                             << "%) navmesh tiles are generated";
        }

        template <class T>
        void serializeToStderr(const T& value)
        {
            const std::vector<std::byte> data = serialize(value);
            Debug::getLockedRawStderr()->write(
                reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        }

        void logGeneratedTilesMessage(std::size_t number)
        {
            serializeToStderr(GeneratedTiles{ static_cast<std::uint64_t>(number) });
        }

        struct LogGeneratedTiles
        {
            void operator()(std::size_t provided, std::size_t expected) const { logGeneratedTiles(provided, expected); }
        };

        class RecastMeshProvider final : public DetourNavigator::RecastMeshProvider
        {
        public:
            explicit RecastMeshProvider(std::shared_ptr<TilesData> tilesData)
                : mTilesData(std::move(tilesData))
            {
            }

            std::shared_ptr<RecastMesh> getMesh(ESM::RefId worldspace, const TilePosition& tilePosition) const override
            {
                return mTilesData->mTileCachedRecastMeshManager.getNewMesh(worldspace, tilePosition);
            }

        private:
            std::shared_ptr<TilesData> mTilesData;
        };

        struct WorldspaceProgress
        {
            std::size_t mExpected = 0;
            std::size_t mProvided = 0;
            std::shared_ptr<RecastMeshProvider> mProvider;
        };

    }

    class NavMeshTileConsumer final : public DetourNavigator::NavMeshTileConsumer
    {
    public:
        explicit NavMeshTileConsumer(NavMeshTileStore& store, const GenerateAllNavMeshTilesOptions& options)
            : mWriteBinaryLog(options.mWriteBinaryLog)
            , mStore(store)
        {
        }

        Status getStatus() const { return mStatus.load(); }

        void expect(ESM::RefId worldspace, std::size_t tiles, std::shared_ptr<RecastMeshProvider> provider)
        {
            {
                const std::lock_guard lock(mProgressMutex);
                mProgress.emplace(
                    worldspace, WorldspaceProgress{ .mExpected = tiles, .mProvider = std::move(provider) });
            }
            const std::size_t expected = mExpected += tiles;
            if (mWriteBinaryLog)
                serializeToStderr(ExpectedTiles{ static_cast<std::uint64_t>(expected) });
        }

        bool holds(const ContentStore::Key& key) override
        {
            const bool held = mStore.holds(key);
            if (held)
                ++mFound;
            return held;
        }

        std::unique_ptr<PreparedNavMeshData> read(const ContentStore::Key& key) override
        {
            std::unique_ptr<PreparedNavMeshData> data = mStore.read(key);
            if (data != nullptr)
                ++mFound;
            return data;
        }

        void write(const ContentStore::Key& key, const PreparedNavMeshData& data) override { mStore.write(key, data); }

        void provide(ESM::RefId worldspace) override { report(worldspace); }

        void cancel(std::string_view reason) override
        {
            Status expected = Status::Ok;
            if (!mStatus.compare_exchange_strong(expected, Status::Cancelled))
                return;
            // Predicate lock prevents missed cancellation wake-ups.
            {
                const std::lock_guard lock(mProvidedMutex);
                mProvidedChanged.notify_all();
            }
        }

        void updateStats(const NavMeshTileConsumerStats& value) override
        {
            const Misc::Locked<GenerateTilesStats> stats = mStats.lock();
            stats->mMaxPolyCountPerTile = std::max(stats->mMaxPolyCountPerTile, value.mPolyCount);
        }

        void waitTilesInFlight(std::size_t limit)
        {
            std::unique_lock lock(mProvidedMutex);
            mProvidedChanged.wait(lock, [&] { return mExpected - mProvided <= limit || mStatus != Status::Ok; });
        }

        GenerateTilesResult finish()
        {
            {
                std::unique_lock lock(mProvidedMutex);
                mProvidedChanged.wait(lock, [&] { return mProvided >= mExpected || mStatus != Status::Ok; });
            }
            // Every write the store was handed is on disk, or counted as failed, before the store's figures are read.
            mStore.flush();
            const ContentStore::Stats store = mStore.getStats();
            if (mExpected > 0)
                logGeneratedTiles(mProvided, mExpected);
            if (mWriteBinaryLog)
                logGeneratedTilesMessage(mProvided);
            return GenerateTilesResult{
                .mStatus = mStatus.load(),
                .mProvided = mProvided.load(),
                .mFound = mFound.load(),
                .mWritten = static_cast<std::size_t>(store.mWritten),
                .mUnwritten = static_cast<std::size_t>(store.mDropped),
                .mStats = *mStats.lockConst(),
            };
        }

    private:
        std::atomic_size_t mExpected{ 0 };
        std::atomic_size_t mProvided{ 0 };
        const bool mWriteBinaryLog;
        std::mutex mProgressMutex;
        std::unordered_map<ESM::RefId, WorldspaceProgress> mProgress;
        Misc::ProgressReporter<LogGeneratedTiles> mReporter;
        Misc::ScopeGuarded<GenerateTilesStats> mStats;
        std::mutex mProvidedMutex;
        std::condition_variable mProvidedChanged;
        std::atomic<Status> mStatus{ Status::Ok };
        std::atomic_size_t mFound{ 0 };
        NavMeshTileStore& mStore;

        void report(ESM::RefId worldspace)
        {
            std::size_t provided;
            {
                // Predicate lock prevents missed wake-ups.
                const std::lock_guard lock(mProvidedMutex);
                provided = mProvided.fetch_add(1, std::memory_order_relaxed) + 1;
                mProvidedChanged.notify_all();
            }
            mReporter(provided, mExpected);
            {
                const std::lock_guard lock(mProgressMutex);
                const auto it = mProgress.find(worldspace);
                Crash::contract(it != mProgress.end(), "progress on a worldspace no job was made for");
                if (++it->second.mProvided == it->second.mExpected)
                {
                    Log(Debug::Info) << "Generated " << it->second.mExpected << " navmesh tiles for " << worldspace
                                     << " worldspace";
                    mProgress.erase(it);
                }
            }
            if (mWriteBinaryLog)
                logGeneratedTilesMessage(provided);
        }
    };

    NavMeshTilesGenerator::NavMeshTilesGenerator(const AgentBounds& agentBounds, const Settings& settings,
        const GenerateAllNavMeshTilesOptions& options, NavMeshTileStore& store, SceneUtil::WorkQueue& workQueue)
        : mAgentBounds(agentBounds)
        , mSettings(settings)
        , mOptions(options)
        , mWorkQueue(workQueue)
        , mConsumer(std::make_shared<NavMeshTileConsumer>(store, options))
    {
    }

    Status NavMeshTilesGenerator::addWorldspace(WorldspaceData&& data)
    {
        mConsumer->waitTilesInFlight(maxTilesInFlight);

        if (mConsumer->getStatus() != Status::Ok)
            return mConsumer->getStatus();

        const std::vector<TilePosition> tiles = std::move(data.mTiles);

        Log(Debug::Info) << "Queued " << tiles.size() << " navmesh tiles for " << data.mWorldspace << " worldspace";

        if (tiles.empty())
            return mConsumer->getStatus();

        const std::shared_ptr<RecastMeshProvider> recastMeshProvider
            = std::make_shared<RecastMeshProvider>(std::move(data.mTilesData));

        mConsumer->expect(data.mWorldspace, tiles.size(), recastMeshProvider);

        for (const TilePosition& tilePosition : tiles)
            mWorkQueue.addWorkItem(std::make_shared<GenerateNavMeshTile>(data.mWorldspace, tilePosition,
                recastMeshProvider, mAgentBounds, mSettings, mOptions.mCollectStats, mConsumer));

        return mConsumer->getStatus();
    }

    GenerateTilesResult NavMeshTilesGenerator::finish()
    {
        return mConsumer->finish();
    }
}
