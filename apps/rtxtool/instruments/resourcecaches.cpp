#include "resourcecaches.hpp"

#include <cstddef>
#include <cstdint>
#include <format>
#include <utility>

#include <osg/Stats>
#include <osg/ref_ptr>

#include <components/resource/resourcesystem.hpp>
#include <components/rtx/common/mebibytes.hpp>

namespace RtxTool
{
    namespace
    {
        constexpr std::string_view sLoaded = " Loaded";

        std::size_t attribute(const osg::Stats& stats, std::uint32_t frame, const std::string& name)
        {
            double value = 0.0;
            stats.getAttribute(frame, name, value);
            return static_cast<std::size_t>(value);
        }
    }

    std::vector<NamedCacheStats> readResourceCaches(const Resource::ResourceSystem& resources)
    {
        const osg::ref_ptr<osg::Stats> stats = new osg::Stats("resource caches", 1);
        const std::uint32_t frame = stats->getLatestFrameNumber();
        resources.reportStats(frame, stats.get());

        std::vector<NamedCacheStats> caches;
        const osg::Stats::AttributeMap& reported = std::as_const(*stats).getAttributeMap(frame);
        for (const auto& [name, value] : reported)
        {
            if (!name.ends_with(sLoaded) || value <= 0.0)
                continue;
            const std::string prefix = name.substr(0, name.size() - sLoaded.size());
            caches.push_back(NamedCacheStats{ .mName = prefix,
                .mStats = Resource::CacheStats{ .mSize = attribute(*stats, frame, prefix + " Count"),
                    .mGet = attribute(*stats, frame, prefix + " Get"),
                    .mHit = attribute(*stats, frame, prefix + " Hit"),
                    .mExpired = attribute(*stats, frame, prefix + " Expired"),
                    .mLoaded = attribute(*stats, frame, prefix + " Loaded"),
                    .mCoalesced = attribute(*stats, frame, prefix + " Coalesced"),
                    .mReloaded = attribute(*stats, frame, prefix + " Reloaded"),
                    .mBytes = attribute(*stats, frame, prefix + " Bytes") } });
        }
        return caches;
    }

    std::string describeResourceCaches(std::span<const NamedCacheStats> caches)
    {
        std::string out;
        for (const NamedCacheStats& cache : caches)
        {
            if (!out.empty())
                out += "; ";
            out += std::format("{} loaded {} ({} again) of {} asks, {} waited on a load in flight, holds {:.1f} MiB",
                cache.mName, cache.mStats.mLoaded, cache.mStats.mReloaded, cache.mStats.mGet, cache.mStats.mCoalesced,
                Rtx::mebibytes(cache.mStats.mBytes));
        }
        return out.empty() ? std::string("no cache loaded anything") : out;
    }
}
