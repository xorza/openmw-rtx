#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace osg
{
    class Stats;
}

namespace Resource
{
    struct CacheStats
    {
        std::size_t mSize = 0;
        std::size_t mGet = 0;
        std::size_t mHit = 0;
        /// Entries let go: by time, or by a budget to make room.
        std::size_t mExpired = 0;

        /// Entries a load put in (`BudgetedObjectCache::getOrLoad`); nought for upstream's cache, which counts none.
        std::size_t mLoaded = 0;

        /// Asks that found their key loading on another thread and waited for that load rather than loading again.
        std::size_t mCoalesced = 0;

        /// Loads of a key the cache had held before and let go: what a longer residency would have kept.
        std::size_t mReloaded = 0;

        /// What the entries hold, as each load stated it: none for a cache whose entries state nothing.
        std::uint64_t mBytes = 0;
    };

    void addCacheStatsAttibutes(std::string_view prefix, std::vector<std::string>& out);

    void reportStats(std::string_view prefix, std::uint32_t frameNumber, const CacheStats& src, osg::Stats& dst);
}
