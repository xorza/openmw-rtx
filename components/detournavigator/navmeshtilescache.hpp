#pragma once

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include <components/resource/cachebudget.hpp>

#include "agentbounds.hpp"
#include "preparednavmeshdata.hpp"
#include "recastmesh.hpp"
#include "tileposition.hpp"

namespace DetourNavigator
{
    struct RecastMeshData
    {
        Mesh mMesh;
        std::vector<CellWater> mWater;
        std::vector<Heightfield> mHeightfields;
        std::vector<FlatHeightfield> mFlatHeightfields;
    };

    inline bool operator<(const RecastMeshData& lhs, const RecastMeshData& rhs)
    {
        return std::tie(lhs.mMesh, lhs.mWater, lhs.mHeightfields, lhs.mFlatHeightfields)
            < std::tie(rhs.mMesh, rhs.mWater, rhs.mHeightfields, rhs.mFlatHeightfields);
    }

    inline bool operator<(const RecastMeshData& lhs, const RecastMesh& rhs)
    {
        return std::tie(lhs.mMesh, lhs.mWater, lhs.mHeightfields, lhs.mFlatHeightfields)
            < std::tie(rhs.getMesh(), rhs.getWater(), rhs.getHeightfields(), rhs.getFlatHeightfields());
    }

    inline bool operator<(const RecastMesh& lhs, const RecastMeshData& rhs)
    {
        return std::tie(lhs.getMesh(), lhs.getWater(), lhs.getHeightfields(), lhs.getFlatHeightfields())
            < std::tie(rhs.mMesh, rhs.mWater, rhs.mHeightfields, rhs.mFlatHeightfields);
    }

    struct NavMeshTilesCacheStats;

    /// The navigation mesh tiles prepared for recast meshes, kept by the caches' byte budget in their one
    /// least-recently-used order (`Resource::CacheBudget`): a tile anything uses stays, and one nothing uses stays
    /// until the caches pass their budget and it is the least recently used of them all.
    class NavMeshTilesCache final : private Resource::BudgetedCache
    {
    public:
        struct Item
        {
            std::atomic<std::int64_t> mUseCount;
            AgentBounds mAgentBounds;
            TilePosition mChangedTile;
            RecastMeshData mRecastMeshData;
            std::unique_ptr<PreparedNavMeshData> mPreparedNavMeshData;
            std::size_t mSize;

            /// The budget's stamp of when it was last let go of, which orders the free items.
            std::uint64_t mUsed = 0;

            Item(const AgentBounds& agentBounds, const TilePosition& changedTile, RecastMeshData&& recastMeshData,
                std::size_t size)
                : mUseCount(0)
                , mAgentBounds(agentBounds)
                , mChangedTile(changedTile)
                , mRecastMeshData(std::move(recastMeshData))
                , mSize(size)
            {
            }
        };

        using ItemIterator = std::list<Item>::iterator;

        class Value
        {
        public:
            Value()
                : mOwner(nullptr)
                , mIterator()
            {
            }

            Value(NavMeshTilesCache& owner, ItemIterator iterator)
                : mOwner(&owner)
                , mIterator(iterator)
            {
            }

            Value(const Value& other) = delete;

            Value(Value&& other) noexcept
                : mOwner(other.mOwner)
                , mIterator(other.mIterator)
            {
                other.mOwner = nullptr;
            }

            ~Value()
            {
                if (mOwner)
                    mOwner->releaseItem(mIterator);
            }

            Value& operator=(const Value& other) = delete;

            Value& operator=(Value&& other) noexcept
            {
                if (mOwner)
                    mOwner->releaseItem(mIterator);

                mOwner = other.mOwner;
                mIterator = other.mIterator;

                other.mOwner = nullptr;

                return *this;
            }

            const PreparedNavMeshData& get() const { return *mIterator->mPreparedNavMeshData; }

            explicit operator bool() const { return mOwner; }

        private:
            NavMeshTilesCache* mOwner;
            ItemIterator mIterator;
        };

        /// @param budget what the caches of the process may hold, which outlives this.
        explicit NavMeshTilesCache(Resource::CacheBudget& budget);
        ~NavMeshTilesCache();

        NavMeshTilesCache(const NavMeshTilesCache&) = delete;
        NavMeshTilesCache& operator=(const NavMeshTilesCache&) = delete;

        Value get(const AgentBounds& agentBounds, const TilePosition& changedTile, const RecastMesh& recastMesh);

        Value set(const AgentBounds& agentBounds, const TilePosition& changedTile, const RecastMesh& recastMesh,
            std::unique_ptr<PreparedNavMeshData>&& value);

        NavMeshTilesCacheStats getStats() const;

    private:
        Resource::CacheBudget& mBudget;
        mutable std::mutex mMutex;
        std::size_t mUsedNavMeshDataSize = 0;
        std::size_t mFreeNavMeshDataSize = 0;
        std::size_t mHitCount = 0;
        std::size_t mGetCount = 0;
        std::list<Item> mBusyItems;

        /// The items nothing uses, the most recently let go of first.
        std::list<Item> mFreeItems;

        /// What evictions took since the last `destroyEvicted`, which the budget's worker destroys.
        std::list<Item> mEvictedItems;

        std::map<std::tuple<AgentBounds, TilePosition, std::reference_wrapper<const RecastMeshData>>, ItemIterator,
            std::less<>>
            mValues;

        std::optional<std::uint64_t> oldestUnheld() final;
        bool evictOldest(std::uint64_t used) final;
        void destroyEvicted() final;

        void acquireItemUnsafe(ItemIterator iterator);

        void releaseItem(ItemIterator iterator);
    };
}
