#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include <components/contentstore/key.hpp>
#include <components/contentstore/store.hpp>
#include <components/esm/refid.hpp>

#include "tileposition.hpp"

namespace DetourNavigator
{
    struct AgentBounds;
    struct PreparedNavMeshData;
    struct RecastSettings;
    class RecastMesh;

    /// The folder the navigator's tiles stand in under the user's cache directory, which the game and
    /// `openmw-navmeshtool` open alike.
    inline constexpr std::string_view sNavMeshTileStoreFolder = "navmesh";

    /// The tiles the navigator generates, kept across sessions in a content store under a key of what made
    /// each, so every profile shares the tiles its content shares (`docs/rtx/architecture.md`, "The navigator's
    /// tiles in the content store"). Nothing is held in memory: every ask is the store reader's, which reaches
    /// the disk, and 95 to 98 % of tiles are small enough that a store holding them would load them all.
    class NavMeshTileStore
    {
    public:
        /// What a key and the tile it names are made by: the key's layout, the recast mesh builder, the
        /// collision a model's file makes, and Recast and Detour. `DetourNavigatorTileVersionTest` holds it.
        static constexpr std::uint32_t sTileVersion = 2;

        /// @param directory where the store stands, made if it is not there.
        explicit NavMeshTileStore(std::filesystem::path directory);

        /// The key of the tile `recastMesh` makes for `agentBounds` at `tilePosition` of `worldspace`, or nothing
        /// where a source's model has no file hash: its path alone would keep a replaced file's tile.
        static std::optional<ContentStore::Key> keyOf(const RecastSettings& settings, const AgentBounds& agentBounds,
            ESM::RefId worldspace, const TilePosition& tilePosition, const RecastMesh& recastMesh);

        /// The tile stored under `key`, or nothing. A payload that does not read is discarded, and the tile made
        /// again in its place is written.
        std::unique_ptr<PreparedNavMeshData> read(const ContentStore::Key& key);

        /// Whether a tile is stored under `key`, with nothing of it read.
        bool holds(const ContentStore::Key& key);

        void write(const ContentStore::Key& key, const PreparedNavMeshData& data);

        /// Waits until every write handed so far is on disk, or counted as failed.
        void flush();

        ContentStore::Stats getStats();

    private:
        ContentStore::Store mStore;
    };
}
