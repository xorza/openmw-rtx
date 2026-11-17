#include <gtest/gtest.h>

#include <components/rtxvulkan/texture/texturearrival.hpp>

namespace Rtx
{
    namespace
    {
        /// **The means grow by doubling within the room.** Holding 10 bytes, a run needing 8 makes nothing; one needing
        /// 12 makes 20, twice what was held; with a room of 15, 15; one needing 40, past twice and past the room, makes
        /// 40. From nothing, a run makes what its group needs, 12.
        TEST(RtxMeansGrownToTest, theMeansGrowByDoublingWithinTheRoom)
        {
            EXPECT_EQ(meansGrownTo(10, 8, 100), 10u);
            EXPECT_EQ(meansGrownTo(10, 12, 100), 20u);
            EXPECT_EQ(meansGrownTo(10, 12, 15), 15u);
            EXPECT_EQ(meansGrownTo(10, 40, 15), 40u);
            EXPECT_EQ(meansGrownTo(0, 12, 100), 12u);
        }
    }
}
