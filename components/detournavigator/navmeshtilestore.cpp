#include "navmeshtilestore.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <exception>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <components/debug/debuglog.hpp>
#include <components/misc/bytebuffer.hpp>
#include <components/misc/compression.hpp>
#include <components/misc/digestwords.hpp>
#include <components/resource/bulletshape.hpp>

#include "agentbounds.hpp"
#include "preparednavmeshdata.hpp"
#include "recastmesh.hpp"
#include "serialization.hpp"
#include "settings.hpp"

namespace DetourNavigator
{
    namespace
    {
        /// The store's one kind: a tile.
        constexpr std::uint32_t sTileKind = 0;

        constexpr ContentStore::Policy sTilePolicy{ .mKindCount = 1, .mHolding = std::nullopt };

        /// The most an LZ4 block expands by: a literal run of one byte and a match of 255 at a time.
        constexpr std::size_t sLz4ExpansionMost = 255;

        /// What `PreparedNavMeshData::mUserId` is for a tile the store answered, which the debug view draws
        /// brighter; a tile generated in this session has 0.
        constexpr unsigned sStoredUserId = 1;

        /// Each value a run of its own, in a fixed order, so no struct's padding reaches the key.
        void addSettings(ContentStore::Digest& digest, const RecastSettings& settings)
        {
            digest.add(settings.mCellHeight);
            digest.add(settings.mCellSize);
            digest.add(settings.mDetailSampleDist);
            digest.add(settings.mDetailSampleMaxError);
            digest.add(settings.mMaxClimb);
            digest.add(settings.mMaxSimplificationError);
            digest.add(settings.mMaxSlope);
            digest.add(settings.mRecastScaleFactor);
            digest.add(settings.mSwimHeightScale);
            digest.add(settings.mBorderSize);
            digest.add(settings.mMaxEdgeLen);
            digest.add(settings.mMaxVertsPerPoly);
            digest.add(settings.mRegionMergeArea);
            digest.add(settings.mRegionMinArea);
            digest.add(settings.mTileSize);
        }

        void addMesh(ContentStore::Digest& digest, const RecastMesh& recastMesh)
        {
            digest.add(static_cast<std::uint64_t>(recastMesh.getWater().size()));
            for (const CellWater& water : recastMesh.getWater())
            {
                digest.add(water.mCellPosition);
                digest.add(water.mWater.mCellSize);
                digest.add(water.mWater.mLevel);
            }

            digest.add(static_cast<std::uint64_t>(recastMesh.getHeightfields().size()));
            for (const Heightfield& heightfield : recastMesh.getHeightfields())
            {
                digest.add(heightfield.mCellPosition);
                digest.add(heightfield.mCellSize);
                digest.add(heightfield.mLength);
                digest.add(heightfield.mMinHeight);
                digest.add(heightfield.mMaxHeight);
                digest.add(std::span<const float>(heightfield.mHeights));
                digest.add(static_cast<std::uint64_t>(heightfield.mOriginalSize));
                digest.add(heightfield.mMinX);
                digest.add(heightfield.mMinY);
            }

            digest.add(static_cast<std::uint64_t>(recastMesh.getFlatHeightfields().size()));
            for (const FlatHeightfield& heightfield : recastMesh.getFlatHeightfields())
            {
                digest.add(heightfield.mCellPosition);
                digest.add(heightfield.mCellSize);
                digest.add(heightfield.mHeight);
            }
        }
    }

    NavMeshTileStore::NavMeshTileStore(std::filesystem::path directory)
        : mStore(std::move(directory), sTilePolicy)
    {
    }

    std::optional<ContentStore::Key> NavMeshTileStore::keyOf(const RecastSettings& settings,
        const AgentBounds& agentBounds, const ESM::RefId worldspace, const TilePosition& tilePosition,
        const RecastMesh& recastMesh)
    {
        ContentStore::Digest digest("navmesh tile", sTileVersion);
        addSettings(digest, settings);
        digest.add(agentBounds.mShapeType);
        digest.add(agentBounds.mHalfExtents);
        const std::string worldspaceText = worldspace.serializeText();
        digest.add(std::span<const char>(worldspaceText));
        digest.add(tilePosition);
        addMesh(digest, recastMesh);

        // **A source is its model's path and the hash of its file's bytes**, which a mod that replaces the file
        // moves, and the transform it stands at; its triangles are what the two make. **Each its own digest, in
        // their order**, as the water and the heights are sorted: the mesh lists its sources as its index finds
        // them, which follows the order the objects were added in, and two starts add them in two orders.
        std::vector<Misc::DigestWords> sources;
        sources.reserve(recastMesh.getMeshSources().size());
        for (const MeshSource& source : recastMesh.getMeshSources())
        {
            if (source.mShape->mFileHash.empty())
                return std::nullopt;
            ContentStore::Digest sourceDigest("navmesh tile source", sTileVersion);
            sourceDigest.add(source.mAreaType);
            sourceDigest.add(std::span<const char>(source.mShape->mFileName.value()));
            sourceDigest.add(std::span<const char>(source.mShape->mFileHash));
            sourceDigest.add(source.mObjectTransform.mPosition);
            sourceDigest.add(source.mObjectTransform.mScale);
            sources.push_back(sourceDigest.getKey().mHash);
        }
        std::sort(sources.begin(), sources.end());
        digest.add(std::span<const Misc::DigestWords>(sources));
        return digest.getKey();
    }

    std::unique_ptr<PreparedNavMeshData> NavMeshTileStore::read(const ContentStore::Key& key)
    {
        Misc::ByteBuffer payload;
        if (!mStore.read(key, sTileKind, sTileVersion, ContentStore::Reach::Disk, payload))
            return nullptr;

        // The payload is what this store wrote, its digest checked, so a payload that does not read is one a
        // fault in this code wrote: discarded, and the tile made again in its place. **The size it states is
        // bounded before it is allocated for**: `Misc::compress` writes it ahead of the LZ4 block, and LZ4
        // expands a block at most 255 times, so a payload that states more is no tile, where an allocation of
        // what a fault wrote there failed or ended the process.
        const bool plausible = [&] {
            std::size_t stated = 0;
            if (payload.size() <= sizeof(stated))
                return false;
            std::memcpy(&stated, payload.data(), sizeof(stated));
            return stated <= (payload.size() - sizeof(stated)) * sLz4ExpansionMost;
        }();
        auto data = std::make_unique<PreparedNavMeshData>();
        try
        {
            if (plausible
                && deserialize(Misc::decompress(std::vector<std::byte>(payload.begin(), payload.end())), *data))
            {
                data->mUserId = sStoredUserId;
                return data;
            }
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "A stored navmesh tile could not be read, and is made again: " << e.what();
        }
        mStore.discard(key);
        return nullptr;
    }

    bool NavMeshTileStore::holds(const ContentStore::Key& key)
    {
        return mStore.holds(key, ContentStore::Reach::Disk);
    }

    void NavMeshTileStore::write(const ContentStore::Key& key, const PreparedNavMeshData& data)
    {
        const std::vector<std::byte> packed = Misc::compress(serialize(data));
        mStore.write(
            key, sTileKind, sTileVersion, Misc::ByteBuffer(packed.begin(), packed.end()), ContentStore::Reach::Disk);
    }

    void NavMeshTileStore::flush()
    {
        mStore.flush();
    }

    ContentStore::Stats NavMeshTileStore::getStats()
    {
        return mStore.getStats();
    }
}
