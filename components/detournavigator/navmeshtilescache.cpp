#include "navmeshtilescache.hpp"

#include <cassert>
#include <cstdint>
#include <iterator>
#include <optional>

#include "stats.hpp"

namespace DetourNavigator
{
    NavMeshTilesCache::NavMeshTilesCache(Resource::CacheBudget& budget)
        : mBudget(budget)
    {
        mBudget.join(*this);
    }

    NavMeshTilesCache::~NavMeshTilesCache()
    {
        mBudget.leave(*this);
        mBudget.remove(mUsedNavMeshDataSize);
    }

    NavMeshTilesCache::Value NavMeshTilesCache::get(
        const AgentBounds& agentBounds, const TilePosition& changedTile, const RecastMesh& recastMesh)
    {
        const std::scoped_lock lock(mMutex);

        ++mGetCount;

        const auto tile = mValues.find(std::tie(agentBounds, changedTile, recastMesh));
        if (tile == mValues.end())
            return Value();

        acquireItemUnsafe(tile->second);

        ++mHitCount;

        return Value(*this, tile->second);
    }

    NavMeshTilesCache::Value NavMeshTilesCache::set(const AgentBounds& agentBounds, const TilePosition& changedTile,
        const RecastMesh& recastMesh, std::unique_ptr<PreparedNavMeshData>&& value)
    {
        const auto itemSize = sizeof(RecastMesh) + getSize(recastMesh)
            + (value == nullptr ? 0 : sizeof(PreparedNavMeshData) + getSize(*value));

        Value handed;
        {
            const std::scoped_lock lock(mMutex);

            RecastMeshData key{ recastMesh.getMesh(), recastMesh.getWater(), recastMesh.getHeightfields(),
                recastMesh.getFlatHeightfields() };

            const auto iterator
                = mFreeItems.emplace(mFreeItems.end(), agentBounds, changedTile, std::move(key), itemSize);
            const auto emplaced = mValues.emplace(
                std::make_tuple(agentBounds, changedTile, std::cref(iterator->mRecastMeshData)), iterator);

            if (!emplaced.second)
            {
                mFreeItems.erase(iterator);
                acquireItemUnsafe(emplaced.first->second);
                ++mGetCount;
                ++mHitCount;
                return Value(*this, emplaced.first->second);
            }

            iterator->mPreparedNavMeshData = std::move(value);
            ++iterator->mUseCount;
            mUsedNavMeshDataSize += itemSize;
            mBudget.add(itemSize);
            mBusyItems.splice(mBusyItems.end(), mFreeItems, iterator);
            handed = Value(*this, iterator);
        }

        // Outside the lock, since making room takes this cache's lock and the others'; the tile is in use here, so
        // the room made is never its own.
        mBudget.makeRoomForInsertion();
        return handed;
    }

    NavMeshTilesCacheStats NavMeshTilesCache::getStats() const
    {
        NavMeshTilesCacheStats result;
        {
            const std::scoped_lock lock(mMutex);
            result.mNavMeshCacheSize = mUsedNavMeshDataSize;
            result.mUsedNavMeshTiles = mBusyItems.size();
            result.mCachedNavMeshTiles = mFreeItems.size();
            result.mHitCount = mHitCount;
            result.mGetCount = mGetCount;
        }
        return result;
    }

    std::optional<std::uint64_t> NavMeshTilesCache::oldestUnheld()
    {
        const std::scoped_lock lock(mMutex);
        if (mFreeItems.empty())
            return std::nullopt;
        return mFreeItems.back().mUsed;
    }

    bool NavMeshTilesCache::evictOldest(const std::uint64_t used)
    {
        const std::scoped_lock lock(mMutex);
        if (mFreeItems.empty() || mFreeItems.back().mUsed != used)
            return false;

        const Item& item = mFreeItems.back();
        const auto value = mValues.find(std::tie(item.mAgentBounds, item.mChangedTile, item.mRecastMeshData));
        assert(value != mValues.end() && "a free tile no key names");
        mValues.erase(value);
        mUsedNavMeshDataSize -= item.mSize;
        mFreeNavMeshDataSize -= item.mSize;
        mBudget.remove(item.mSize);
        mEvictedItems.splice(mEvictedItems.end(), mFreeItems, std::prev(mFreeItems.end()));
        return true;
    }

    void NavMeshTilesCache::destroyEvicted()
    {
        std::list<Item> evicted;
        {
            const std::scoped_lock lock(mMutex);
            evicted.swap(mEvictedItems);
        }
        // Here, outside the lock.
        evicted.clear();
    }

    void NavMeshTilesCache::acquireItemUnsafe(ItemIterator iterator)
    {
        if (++iterator->mUseCount > 1)
            return;

        mBusyItems.splice(mBusyItems.end(), mFreeItems, iterator);
        mFreeNavMeshDataSize -= iterator->mSize;
    }

    void NavMeshTilesCache::releaseItem(ItemIterator iterator)
    {
        if (--iterator->mUseCount > 0)
            return;

        const std::scoped_lock lock(mMutex);

        iterator->mUsed = mBudget.stamp();
        mFreeItems.splice(mFreeItems.begin(), mBusyItems, iterator);
        mFreeNavMeshDataSize += iterator->mSize;
    }
}
