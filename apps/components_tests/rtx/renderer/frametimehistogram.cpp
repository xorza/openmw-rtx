#include <gtest/gtest.h>

#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/frametimehistogram.hpp>

namespace Rtx
{
    namespace
    {
        FrameSpend frameOf(const double ms)
        {
            FrameSpend spend;
            spend.at(Timing::Frame) = ms;
            return spend;
        }

        /// **A session's frames summarised to a tenth of a millisecond, with where its worst frame went**:
        /// 98 frames of 16.66 ms fall in the bin 16.6 to 16.7, so the median, the 50th frame by nearest rank,
        /// reads 16.7; the 99th is the frame of 40 ms, in the bin 40.0 to 40.1; the worst, 300 ms, is past the
        /// range and kept exactly, with its frame's place in the stretch and its three largest stretches. Two
        /// hitches: the 40 ms frame, past twice the median's 33.4, and the one past the range. A cleared
        /// stretch has no frames, and its first frame is its worst even when it took no time at all.
        TEST(RtxFrameTimeHistogramTest, aSessionIsSummarisedToATenthOfAMillisecond)
        {
            FrameTimeHistogram times;
            EXPECT_EQ(describe(times.summarise()), "no frames");

            for (int frame = 0; frame < 49; ++frame)
                times.add(frameOf(16.66));
            FrameSpend worst = frameOf(300.0);
            worst.at(Timing::Update) = 250.0;
            worst.at(Timing::Place) = 30.0;
            worst.at(Timing::Upload) = 29.0;
            worst.at(Timing::Walk) = 15.0;
            worst.at(Timing::Trace) = 5.0;
            times.add(worst);
            for (int frame = 0; frame < 49; ++frame)
                times.add(frameOf(16.66));
            times.add(frameOf(40.0));

            const FrameTimeHistogram::Summary summary = times.summarise();
            EXPECT_EQ(summary.mFrames, 100u);
            EXPECT_DOUBLE_EQ(summary.mMedianMs, 16.7);
            EXPECT_DOUBLE_EQ(summary.mP99Ms, 40.1);
            EXPECT_DOUBLE_EQ(summary.mWorst.at(Timing::Frame), 300.0);
            EXPECT_EQ(summary.mWorstAt, 50u) << "after the first 49";
            EXPECT_EQ(summary.mHitches, 2u);
            // `Upload` is a share of `Place` and no stretch of its own, so `walk` is the third.
            EXPECT_EQ(describe(summary),
                "100 frames, median 16.7 ms, p99 40.1 ms, worst 300.0 ms at frame 50 (update 250.0 place 30.0 walk "
                "15.0), 2 hitches");

            // A frame just inside twice the median is no hitch: 33.3 ms against 33.4. And no quantile
            // reads past the worst, which its bin's edge would. Stretches that tie stand in `Timing`'s order.
            FrameTimeHistogram even;
            even.add(frameOf(16.66));
            even.add(frameOf(16.66));
            even.add(frameOf(33.3));
            EXPECT_EQ(even.summarise().mHitches, 0u);
            EXPECT_EQ(describe(even.summarise()),
                "3 frames, median 16.7 ms, p99 33.3 ms, worst 33.3 ms at frame 3 (finish 0.0 walk 0.0 place 0.0), 0 "
                "hitches");

            times.clear();
            EXPECT_EQ(times.summarise().mFrames, 0u);
            times.add(frameOf(0.0));
            EXPECT_EQ(times.summarise().mWorstAt, 1u) << "the first frame is the worst of one";
        }

        /// **A frame closes when the next arrives**: its own work, with the arriving frame's whole span, update
        /// and hold, since those are what the arrival measured.
        TEST(RtxFrameTimeHistogramTest, aFrameClosesWithTheArrivingFramesSpanUpdateAndHold)
        {
            FrameSpend behind;
            behind.at(Timing::Frame) = 1.0;
            behind.at(Timing::Update) = 2.0;
            behind.at(Timing::Sleep) = 3.0;
            behind.at(Timing::Walk) = 4.0;
            FrameSpend arriving;
            arriving.at(Timing::Frame) = 10.0;
            arriving.at(Timing::Update) = 20.0;
            arriving.at(Timing::Sleep) = 30.0;
            arriving.at(Timing::Walk) = 40.0;

            const FrameSpend closed = behind.closedBy(arriving);
            EXPECT_EQ(closed.at(Timing::Frame), 10.0);
            EXPECT_EQ(closed.at(Timing::Update), 20.0);
            EXPECT_EQ(closed.at(Timing::Sleep), 30.0);
            EXPECT_EQ(closed.at(Timing::Walk), 4.0) << "the work is the frame behind's";
        }
    }
}
