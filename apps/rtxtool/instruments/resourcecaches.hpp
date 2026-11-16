#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <components/resource/cachestats.hpp>

namespace Resource
{
    class ResourceSystem;
}

namespace RtxTool
{
    /// One of the game's resource caches as its stats name it (`Resource::reportStats`'s prefix) and what it counted.
    struct NamedCacheStats
    {
        std::string mName;
        Resource::CacheStats mStats;
    };

    /// Every cache `resources` reports that has loaded anything, in the order its stats list them: read through
    /// `Resource::ResourceSystem::reportStats`, the profiler's own route, so a cache it reports is one this reads.
    std::vector<NamedCacheStats> readResourceCaches(const Resource::ResourceSystem& resources);

    /// The caches as one line, each `<name> loaded <n> (<r> again) of <g> asks, <c> waited on a load in flight, holds
    /// <b> MiB`: what a run's loaders read, how much of it a cache that kept more would have saved, and what each
    /// keeps now.
    std::string describeResourceCaches(std::span<const NamedCacheStats> caches);
}
