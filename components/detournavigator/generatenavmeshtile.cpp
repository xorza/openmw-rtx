#include "generatenavmeshtile.hpp"

#include "makenavmesh.hpp"
#include "navmeshdata.hpp"
#include "navmeshtilestore.hpp"
#include "preparednavmeshdata.hpp"
#include "settings.hpp"

#include <components/debug/debuglog.hpp>

#include <DetourNavMesh.h>

#include <osg/io_utils>

#include <memory>
#include <optional>
#include <vector>

namespace DetourNavigator
{
    namespace
    {
        /// Tells the consumer a tile is done with as the work ends, by whichever way it ends but a cancel.
        struct Provide
        {
            ESM::RefId mWorldspace;
            std::shared_ptr<NavMeshTileConsumer> mConsumer;

            ~Provide() noexcept
            {
                if (mConsumer != nullptr)
                    mConsumer->provide(mWorldspace);
            }
        };

        void reportPolyCount(NavMeshTileConsumer& consumer, const NavMeshData& tileData)
        {
            const dtMeshHeader& header = *reinterpret_cast<dtMeshHeader*>(tileData.mValue.get());
            consumer.updateStats(NavMeshTileConsumerStats{ .mPolyCount = header.polyCount });
        }
    }

    GenerateNavMeshTile::GenerateNavMeshTile(ESM::RefId worldspace, const TilePosition& tilePosition,
        std::weak_ptr<const RecastMeshProvider> recastMeshProvider, const AgentBounds& agentBounds,
        const DetourNavigator::Settings& settings, bool collectStats, std::weak_ptr<NavMeshTileConsumer> consumer)
        : mWorldspace(worldspace)
        , mTilePosition(tilePosition)
        , mRecastMeshProvider(std::move(recastMeshProvider))
        , mAgentBounds(agentBounds)
        , mSettings(settings)
        , mCollectStats(collectStats)
        , mConsumer(std::move(consumer))
    {
    }

    void GenerateNavMeshTile::doWork()
    {
        impl();
    }

    void GenerateNavMeshTile::impl() noexcept
    {
        try
        {
            const std::shared_ptr<NavMeshTileConsumer> consumer = mConsumer.lock();

            if (consumer == nullptr)
                return;

            Provide provide{ mWorldspace, consumer };

            const std::shared_ptr<const RecastMeshProvider> recastMeshProvider = mRecastMeshProvider.lock();

            if (recastMeshProvider == nullptr)
                return;

            const std::shared_ptr<RecastMesh> recastMesh = recastMeshProvider->getMesh(mWorldspace, mTilePosition);

            if (recastMesh == nullptr || isEmpty(*recastMesh))
                return;

            const std::optional<ContentStore::Key> key
                = NavMeshTileStore::keyOf(mSettings.mRecast, mAgentBounds, mWorldspace, mTilePosition, *recastMesh);

            // A stored tile is read only where its polygons are counted, and otherwise only found.
            if (key.has_value() && mCollectStats)
            {
                if (const std::unique_ptr<PreparedNavMeshData> stored = consumer->read(*key))
                {
                    reportPolyCount(
                        *consumer, makeNavMeshTileData(*stored, {}, mAgentBounds, mTilePosition, mSettings.mRecast));
                    return;
                }
            }
            else if (key.has_value() && consumer->holds(*key))
                return;

            const std::unique_ptr<PreparedNavMeshData> data
                = prepareNavMeshTileData(*recastMesh, mWorldspace, mTilePosition, mAgentBounds, mSettings.mRecast);

            if (data == nullptr)
                return;

            // Verify generated data before consuming.
            const NavMeshData tileData = makeNavMeshTileData(*data, {}, mAgentBounds, mTilePosition, mSettings.mRecast);

            if (key.has_value())
                consumer->write(*key, *data);

            if (mCollectStats)
                reportPolyCount(*consumer, tileData);
        }
        catch (const std::exception& e)
        {
            if (const std::shared_ptr<NavMeshTileConsumer> consumer = mConsumer.lock())
            {
                Log(Debug::Warning) << "Failed to generate navmesh for worldspace \"" << mWorldspace << "\" tile "
                                    << mTilePosition << ": " << e.what();
                consumer->cancel(e.what());
            }
        }
    }
}
