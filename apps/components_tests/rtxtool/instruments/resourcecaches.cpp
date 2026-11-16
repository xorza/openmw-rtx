#include <array>

#include <gtest/gtest.h>

#include <apps/rtxtool/instruments/resourcecaches.hpp>

namespace RtxTool
{
    namespace
    {
        /// **Each cache says what it loaded, how much of that again, of how many asks, and how many waited**, in
        /// the order given; and a run whose caches loaded nothing says so.
        TEST(RtxResourceCachesTest, eachCacheSaysWhatItLoadedAgainAndWaitedOn)
        {
            const std::array caches{
                NamedCacheStats{ .mName = "Node",
                    .mStats = { .mSize = 9,
                        .mGet = 120,
                        .mHit = 80,
                        .mLoaded = 40,
                        .mCoalesced = 3,
                        .mReloaded = 12,
                        .mBytes = 3 * 1024 * 1024 + 512 * 1024 } },
                NamedCacheStats{ .mName = "Image", .mStats = { .mGet = 7, .mLoaded = 7 } },
            };
            EXPECT_EQ(describeResourceCaches(caches),
                "Node loaded 40 (12 again) of 120 asks, 3 waited on a load in flight, holds 3.5 MiB; "
                "Image loaded 7 (0 again) of 7 asks, 0 waited on a load in flight, holds 0.0 MiB");
            EXPECT_EQ(describeResourceCaches({}), "no cache loaded anything");
        }
    }
}
