#ifndef OPENMW_NAVMESHTOOL_NAVMESH_H
#define OPENMW_NAVMESHTOOL_NAVMESH_H

#include <cstddef>
#include <memory>

namespace DetourNavigator
{
    class NavMeshTileStore;
    struct Settings;
    struct AgentBounds;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace NavMeshTool
{
    struct WorldspaceData;
    class NavMeshTileConsumer;

    struct GenerateAllNavMeshTilesOptions
    {
        bool mWriteBinaryLog;
        bool mCollectStats;
    };

    enum class Status
    {
        Ok,
        Cancelled,
    };

    struct GenerateTilesStats
    {
        int mMaxPolyCountPerTile = 0;
    };

    struct GenerateTilesResult
    {
        Status mStatus;
        std::size_t mProvided;

        /// Tiles the store held, tiles written to it, and writes that failed, which the store logged.
        std::size_t mFound;
        std::size_t mWritten;
        std::size_t mUnwritten;
        GenerateTilesStats mStats;
    };

    class NavMeshTilesGenerator
    {
    public:
        NavMeshTilesGenerator(const DetourNavigator::AgentBounds& agentBounds,
            const DetourNavigator::Settings& settings, const GenerateAllNavMeshTilesOptions& options,
            DetourNavigator::NavMeshTileStore& store, SceneUtil::WorkQueue& workQueue);

        Status addWorldspace(WorldspaceData&& data);

        GenerateTilesResult finish();

    private:
        const DetourNavigator::AgentBounds& mAgentBounds;
        const DetourNavigator::Settings& mSettings;
        const GenerateAllNavMeshTilesOptions mOptions;
        SceneUtil::WorkQueue& mWorkQueue;
        std::shared_ptr<NavMeshTileConsumer> mConsumer;
    };
}

#endif
