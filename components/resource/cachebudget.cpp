#include "cachebudget.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace Resource
{
    std::uint64_t CacheBudget::derive(std::optional<std::uint64_t> available, std::uint64_t held)
    {
        if (!available.has_value())
            return sUnbounded;
        const std::uint64_t room = *available + held;
        return room - std::min(room, sReserveBytes);
    }

    CacheBudget::CacheBudget(std::uint64_t bytes)
        : mBytes(bytes)
    {
    }

    CacheBudget::~CacheBudget()
    {
        assert(mCaches.empty() && "a cache outlived the budget it was held to");
    }

    void CacheBudget::join(BudgetedCache& cache)
    {
        const std::scoped_lock lock(mMutex);
        assert(std::ranges::find(mCaches, &cache) == mCaches.end() && "a cache joined its budget twice");
        mCaches.push_back(&cache);
    }

    void CacheBudget::leave(BudgetedCache& cache)
    {
        const std::scoped_lock lock(mMutex);
        const auto found = std::ranges::find(mCaches, &cache);
        assert(found != mCaches.end() && "a cache left a budget it never joined");
        mCaches.erase(found);
    }

    void CacheBudget::makeRoomForInsertion()
    {
        if (getHeld() > getBytes() && !mStarved.load(std::memory_order_relaxed))
            evictToBudget();
    }

    void CacheBudget::makeRoom()
    {
        mStarved.store(false, std::memory_order_relaxed);
        if (getHeld() > getBytes())
            evictToBudget();
    }

    void CacheBudget::remove(std::uint64_t bytes)
    {
        [[maybe_unused]] const std::uint64_t before = mHeld.fetch_sub(bytes, std::memory_order_relaxed);
        assert(before >= bytes && "a cache let go of more than it held");
    }

    void CacheBudget::setBytes(std::uint64_t bytes)
    {
        mBytes.store(bytes, std::memory_order_relaxed);
        makeRoom();
    }

    void CacheBudget::evictToBudget()
    {
        const std::scoped_lock lock(mMutex);

        // An eviction fails where its entry was used between the two asks, which moved it to the front: a race,
        // and more than four a cache in a row end the look rather than spin on it.
        const std::size_t failuresMax = 4 * mCaches.size();
        std::size_t failures = 0;
        while (getHeld() > getBytes() && failures <= failuresMax)
        {
            BudgetedCache* oldest = nullptr;
            std::uint64_t oldestUsed = std::numeric_limits<std::uint64_t>::max();
            for (BudgetedCache* const cache : mCaches)
            {
                const std::optional<std::uint64_t> used = cache->oldestUnheld();
                if (used.has_value() && *used < oldestUsed)
                {
                    oldest = cache;
                    oldestUsed = *used;
                }
            }

            if (oldest == nullptr)
            {
                mStarved.store(true, std::memory_order_relaxed);
                mStarvedLooks.fetch_add(1, std::memory_order_relaxed);
                return;
            }

            if (oldest->evictOldest(oldestUsed))
                failures = 0;
            else
                ++failures;
        }
    }
}
