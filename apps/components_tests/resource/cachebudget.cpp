#include <cstdint>
#include <optional>

#include <gtest/gtest.h>

#include <components/resource/cachebudget.hpp>

namespace Resource
{
    namespace
    {
        /// **The budget is what the caches hold, what the system leaves beside them and what the heap holds free, less
        /// 1 GiB**: 8 GiB left and none held is 7 GiB; 8 left and 2 held, 9; and 3 the heap holds free beside them,
        /// 12; half a GiB left and 3 held, 2.5, under what they hold, so they make room; under the reserve
        /// altogether, none; where the system states nothing, a budget nothing passes.
        TEST(ResourceCacheBudgetTest, theBudgetIsWhatTheCachesHoldAndWhatTheSystemLeavesLessTheReserve)
        {
            constexpr std::uint64_t gib = std::uint64_t{ 1 } << 30;
            EXPECT_EQ(CacheBudget::derive(8 * gib, 0, 0), 7 * gib);
            EXPECT_EQ(CacheBudget::derive(8 * gib, 2 * gib, 0), 9 * gib);
            EXPECT_EQ(CacheBudget::derive(8 * gib, 2 * gib, 3 * gib), 12 * gib)
                << "the heap's free memory, the process's";
            EXPECT_EQ(CacheBudget::derive(gib / 2, 3 * gib, 0), 5 * gib / 2);
            EXPECT_EQ(CacheBudget::derive(gib / 4, gib / 2, 0), 0);
            EXPECT_EQ(CacheBudget::derive(std::nullopt, 2 * gib, 3 * gib), CacheBudget::sUnbounded);
        }

        /// **The budget is derived at each ask from what the system derives, the device's claim and the limit**: 10
        /// derived less a claim of 3 is 7, held to a limit of 5 is 5; the limit let go, 7; the claim gone, 10.
        TEST(ResourceCacheBudgetTest, theBudgetIsTheDerivedLessTheClaimHeldToTheLimit)
        {
            CacheBudget budget(10);
            budget.yieldTo(3);
            EXPECT_EQ(budget.getBytes(), 7u);
            budget.limit(5);
            EXPECT_EQ(budget.getBytes(), 5u);
            budget.limit(std::nullopt);
            EXPECT_EQ(budget.getBytes(), 7u);
            budget.yieldTo(0);
            EXPECT_EQ(budget.getBytes(), 10u);
            budget.yieldTo(30);
            EXPECT_EQ(budget.getBytes(), 0u) << "a claim past what the system derives leaves nothing";
        }
    }
}
