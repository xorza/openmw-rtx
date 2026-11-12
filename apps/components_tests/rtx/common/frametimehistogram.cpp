#include <gtest/gtest.h>

#include <components/rtx/common/frametimehistogram.hpp>

namespace Rtx
{
    namespace
    {
        /// **A session's frames summarised to a tenth of a millisecond**: 98 frames of 16.66 ms fall in
        /// the bin 16.6 to 16.7, so the median, the 50th frame by nearest rank, reads 16.7; the 99th is
        /// the frame of 40 ms, in the bin 40.0 to 40.1; the worst, 300 ms, is past the range and kept
        /// exactly. Two hitches: the 40 ms frame, past twice the median's 33.4, and the one past the
        /// range. A cleared stretch has no frames.
        TEST(RtxFrameTimeHistogramTest, aSessionIsSummarisedToATenthOfAMillisecond)
        {
            FrameTimeHistogram times;
            EXPECT_EQ(describe(times.summarise()), "no frames");

            for (int frame = 0; frame < 98; ++frame)
                times.add(16.66);
            times.add(40.0);
            times.add(300.0);

            const FrameTimeHistogram::Summary summary = times.summarise();
            EXPECT_EQ(summary.mFrames, 100u);
            EXPECT_DOUBLE_EQ(summary.mMedianMs, 16.7);
            EXPECT_DOUBLE_EQ(summary.mP99Ms, 40.1);
            EXPECT_DOUBLE_EQ(summary.mWorstMs, 300.0);
            EXPECT_EQ(summary.mHitches, 2u);
            EXPECT_EQ(describe(summary), "100 frames, median 16.7 ms, p99 40.1 ms, worst 300.0 ms, 2 hitches");

            // A frame just inside twice the median is no hitch: 33.3 ms against 33.4. And no quantile
            // reads past the worst, which its bin's edge would.
            FrameTimeHistogram even;
            even.add(16.66);
            even.add(16.66);
            even.add(33.3);
            EXPECT_EQ(even.summarise().mHitches, 0u);
            EXPECT_EQ(describe(even.summarise()), "3 frames, median 16.7 ms, p99 33.3 ms, worst 33.3 ms, 0 hitches");

            times.clear();
            EXPECT_EQ(times.summarise().mFrames, 0u);
            times.add(5.0);
            EXPECT_EQ(describe(times.summarise()), "1 frame, median 5.0 ms, p99 5.0 ms, worst 5.0 ms, 0 hitches");
        }
    }
}
