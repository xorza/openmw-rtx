#include <gtest/gtest.h>

#include <components/rtx/mirror/pastroom.hpp>

namespace Rtx
{
    namespace
    {
        /// **`rooms-held` names each table that grew and how often**, in the struct's order, and the total counts them
        /// all: frozen 2 and the ring's models 1 total 3. None grown says so.
        TEST(RtxPastRoomTest, eachTableThatGrewIsNamedWithItsCount)
        {
            const PastRoom grown{ .mFrozen = 2, .mRingModels = 1 };
            EXPECT_EQ(grown.total(), 3u);
            EXPECT_EQ(describe(grown), "grew past their room: frozen 2, ring models 1");

            EXPECT_EQ(PastRoom{}.total(), 0u);
            EXPECT_EQ(describe(PastRoom{}), "no table grew past its room");
        }
    }
}
