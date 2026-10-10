#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/device/harness.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/trace/denoise/denoisehistory.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>

namespace Rtx
{
    namespace
    {
        using RtxDenoiseHistoryTest = Testing::DeviceTest;

        /// **A frame reads the held distances back at the scale they were stored at**, which is the
        /// frame before's and not its own. A far plane of 200000 stores at `RANGE / 200000`; the
        /// first frame has nothing stored and reads at its own; a frame at a far plane of 100000
        /// reads what the first stored at the first's scale, and the frame after it at its own,
        /// `RANGE / 100000`, twice the first. The two scales differ, so the order is not a guess.
        TEST_F(RtxDenoiseHistoryTest, aFrameReadsHeldDistancesAtTheScaleTheyWereStoredAt)
        {
            DenoiseHistory history(getDevice(), MemoryUse::Frame, TracePast::Kept);
            const float first = DenoiseHistory::distanceScaleFor(200000.0f);
            const float second = DenoiseHistory::distanceScaleFor(100000.0f);
            ASSERT_EQ(second, 2.0f * first);

            EXPECT_EQ(history.exchangeDistanceScale(first), first) << "nothing stored, read at its own";
            EXPECT_EQ(history.exchangeDistanceScale(second), first) << "read at its own, not the first's that stored";
            EXPECT_EQ(history.exchangeDistanceScale(second), second)
                << "read at the first's, though the second stored since";
        }
    }
}
