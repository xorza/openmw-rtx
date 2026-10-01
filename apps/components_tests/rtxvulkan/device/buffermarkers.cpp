#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/buffermarkers.hpp>
#include <components/rtxvulkan/device/device.hpp>

namespace Rtx
{
    namespace
    {
        /// **A marker names the checkpoint it was handed out for, for as long as the ring keeps it.**
        /// The numbers count from one; nought is a word nothing wrote. The 256th number after a
        /// marker's is the last that leaves it named, since its place in the ring is the next one's.
        TEST(RtxMarkerRingTest, aMarkerNamesItsCheckpointUntilTheRingComesRound)
        {
            const std::array<Checkpoint, 3> zones{ Checkpoint{ "tlas", 7 }, Checkpoint{ "trace", 7 },
                Checkpoint{ "filter", 7 } };

            MarkerRing ring;
            EXPECT_EQ(ring.find(0), nullptr) << "nought before anything was marked";
            EXPECT_EQ(ring.find(1), nullptr) << "a number not yet handed out";

            EXPECT_EQ(ring.mark(&zones[0]), 1u);
            EXPECT_EQ(ring.mark(&zones[1]), 2u);
            EXPECT_EQ(ring.mark(&zones[2]), 3u);
            EXPECT_EQ(ring.find(0), nullptr);
            EXPECT_EQ(ring.find(1), &zones[0]);
            EXPECT_EQ(ring.find(2), &zones[1]);
            EXPECT_EQ(ring.find(3), &zones[2]);

            // Numbers 4 to 256: 255 after the first, which is still named, and the last free place.
            for (std::uint32_t at = 4; at <= MarkerRing::sKept; ++at)
                ring.mark(&zones[2]);
            EXPECT_EQ(ring.find(1), &zones[0]);

            // Number 257 takes the first's place.
            EXPECT_EQ(ring.mark(&zones[1]), MarkerRing::sKept + 1);
            EXPECT_EQ(ring.find(1), nullptr) << "a number whose place was taken again";
            EXPECT_EQ(ring.find(2), &zones[1]);
            EXPECT_EQ(ring.find(MarkerRing::sKept + 1), &zones[1]);
        }
    }
}
