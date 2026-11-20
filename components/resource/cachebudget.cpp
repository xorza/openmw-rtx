#include "cachebudget.hpp"

#include <algorithm>
#include <cassert>
#include <limits>

namespace Resource
{
    std::uint64_t CacheBudget::derive(
        const std::optional<std::uint64_t> available, const std::uint64_t held, const std::uint64_t heapFree)
    {
        if (!available.has_value())
            return sUnbounded;
        const std::uint64_t room = *available + held + heapFree;
        return room - std::min(room, sReserveBytes);
    }

    CacheBudget::CacheBudget(std::uint64_t bytes)
        : mDerived(bytes)
    {
    }

    std::uint64_t CacheBudget::getBytes() const
    {
        const std::uint64_t derived = mDerived.load(std::memory_order_relaxed);
        const std::uint64_t left = derived - std::min(derived, mClaim.load(std::memory_order_relaxed));
        return std::min(left, mLimit.load(std::memory_order_relaxed));
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

    void CacheBudget::makeRoomForInsertion(const std::uint64_t bytes)
    {
        if (getHeld() > getBytes() && !mStarved.load(std::memory_order_relaxed))
            evictToBudget(bytes);
    }

    void CacheBudget::makeRoom()
    {
        mStarved.store(false, std::memory_order_relaxed);
        if (getHeld() > getBytes())
            evictToBudget(sUnbounded);
        destroyEvicted();
    }

    void CacheBudget::destroyEvicted()
    {
        const std::scoped_lock lock(mMutex);
        for (BudgetedCache* const cache : mCaches)
            cache->destroyEvicted();
    }

    void CacheBudget::remove(std::uint64_t bytes)
    {
        [[maybe_unused]] const std::uint64_t before = mHeld.fetch_sub(bytes, std::memory_order_relaxed);
        assert(before >= bytes && "a cache let go of more than it held");
    }

    void CacheBudget::setBytes(std::uint64_t bytes)
    {
        mDerived.store(bytes, std::memory_order_relaxed);
        makeRoom();
    }

    void CacheBudget::limit(std::optional<std::uint64_t> bytes)
    {
        mLimit.store(bytes.value_or(sUnbounded), std::memory_order_relaxed);
        makeRoom();
    }

    void CacheBudget::yieldTo(std::uint64_t claim)
    {
        mClaim.store(claim, std::memory_order_relaxed);
    }

    void CacheBudget::evictToBudget(const std::uint64_t most)
    {
        const std::scoped_lock lock(mMutex);

        // What went is read off what the caches hold, which another thread's insertion may raise meanwhile: then this
        // evicts less than `most`, and the next look the rest.
        const std::uint64_t before = getHeld();
        const auto evicted = [&] {
            const std::uint64_t now = getHeld();
            return before > now ? before - now : 0;
        };

        // An eviction fails where its entry was used between the two asks, which moved it to the front: a race,
        // and more than four a cache in a row end the look rather than spin on it.
        const std::size_t failuresMax = 4 * mCaches.size();
        std::size_t failures = 0;
        while (getHeld() > getBytes() && evicted() < most && failures <= failuresMax)
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
