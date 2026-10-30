#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/zonecounters.hpp>

namespace Rtx
{
    namespace
    {
        /// **Each counter in the driver's order that still shares the one pass.** Of six, the fourth
        /// cannot share a pass with the third and the fifth takes two alone, so 0, 1, 2 and 5 are
        /// read; of twenty that all share one, the first `sMaxZoneCounters`.
        TEST(RtxZoneCountersTest, theCountersReadAreTheOnesThatShareOnePass)
        {
            const auto passesOf = [](const std::span<const std::uint32_t> counters) -> std::uint32_t {
                const auto holds
                    = [&](std::uint32_t counter) { return std::ranges::find(counters, counter) != counters.end(); };
                return (holds(2) && holds(3)) || holds(4) ? 2 : 1;
            };

            std::vector<std::uint32_t> chosen{ 9 };
            ZoneCounters::pickOnePass(6, passesOf, chosen);
            EXPECT_EQ(chosen, (std::vector<std::uint32_t>{ 0, 1, 2, 5 }));

            ZoneCounters::pickOnePass(
                20, [](std::span<const std::uint32_t>) { return 1u; }, chosen);
            ASSERT_EQ(chosen.size(), sMaxZoneCounters);
            EXPECT_EQ(chosen.front(), 0u);
            EXPECT_EQ(chosen.back(), sMaxZoneCounters - 1);

            ZoneCounters::pickOnePass(0, passesOf, chosen);
            EXPECT_TRUE(chosen.empty()) << "a driver that offers none reads none";
        }
    }
}
