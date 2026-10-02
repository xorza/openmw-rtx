#include <cstdint>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/measurewindow.hpp>
#include <components/rtx/renderer/framespend.hpp>

namespace RtxTool
{
    namespace
    {
        WindowFrame standing(std::uint32_t cellsToStand, double frameMs = 10.0)
        {
            WindowFrame frame;
            frame.mWhole = cellsToStand == 0;
            frame.mCellsToStand = cellsToStand;
            frame.mSpend.at(Rtx::Timing::Frame) = frameMs;
            return frame;
        }

        WindowFrame paused()
        {
            WindowFrame frame = standing(0);
            frame.mPaused = true;
            return frame;
        }

        /// Takes `frames` and gives back the outcome of each.
        std::vector<MeasureWindow::Outcome> takeAll(MeasureWindow& window, const std::vector<WindowFrame>& frames)
        {
            std::vector<MeasureWindow::Outcome> outcomes;
            for (const WindowFrame& frame : frames)
                outcomes.push_back(window.take(frame).mOutcome);
            return outcomes;
        }

        using Outcome = MeasureWindow::Outcome;

        /// **The world arrives, stands whole, warms up, and the frame after is measured.** Cells to
        /// stand 5, 3, 3, 0: the fourth frame is whole. A warm-up of two runs on the fifth and the
        /// sixth, so the seventh is the first measured frame, numbered one.
        TEST(RtxMeasureWindowTest, aStopIsMeasuredFromTheFrameAfterTheWarmUpThatFollowsTheWholeWorld)
        {
            MeasureWindow window(2, 30, false);
            EXPECT_EQ(window.getMeasuredIndex(), std::nullopt);

            const std::vector<Outcome> ahead
                = takeAll(window, { standing(5), standing(3), standing(3), standing(0), standing(0) });
            EXPECT_EQ(ahead, std::vector<Outcome>(5, Outcome::Ahead));
            EXPECT_EQ(window.getWaited(), 4u) << "the wait counts the frame that stood whole";
            EXPECT_EQ(window.getMeasuredIndex(), std::nullopt) << "one frame of the warm-up is still to run";

            EXPECT_EQ(window.take(standing(0)).mOutcome, Outcome::Ahead);
            EXPECT_EQ(window.getMeasuredIndex(), 0u) << "the frame about to be taken opens the measurement";

            const MeasureWindow::Taken first = window.take(standing(0));
            EXPECT_TRUE(first.mOpened);
            EXPECT_EQ(first.mOutcome, Outcome::Measured);
            EXPECT_EQ(first.mDrawn, 1u);
            EXPECT_EQ(window.getSeen(), 7u);

            const MeasureWindow::Taken second = window.take(standing(0));
            EXPECT_FALSE(second.mOpened);
            EXPECT_EQ(second.mDrawn, 2u);
            EXPECT_EQ(window.getMeasuredIndex(), 2u);
        }

        /// A world whole on its first frame, with no warm-up, is measured from its second.
        TEST(RtxMeasureWindowTest, aWorldWholeAtOnceIsMeasuredFromTheNextFrame)
        {
            MeasureWindow window(0, 30, false);
            EXPECT_EQ(window.take(standing(0)).mOutcome, Outcome::Ahead);
            EXPECT_EQ(window.getMeasuredIndex(), 0u);
            EXPECT_TRUE(window.take(standing(0)).mOpened);
            EXPECT_EQ(window.getWaited(), 1u);
        }

        /// **A paused frame counts toward neither the wait nor the warm-up**, and the first one says
        /// so once. With a warm-up of two, a pause between them delays the first measured frame by
        /// one. In a session somebody plays, the same frame counts as running.
        TEST(RtxMeasureWindowTest, aPausedFrameAheadOfTheMeasurementDelaysItByOne)
        {
            MeasureWindow window(2, 30, false);
            window.take(standing(0));
            EXPECT_FALSE(window.take(standing(0)).mFirstPause);
            const MeasureWindow::Taken pause = window.take(paused());
            EXPECT_TRUE(pause.mFirstPause);
            EXPECT_FALSE(window.take(paused()).mFirstPause) << "named once";
            EXPECT_EQ(window.getWarmedPaused(), 2u);
            EXPECT_EQ(window.take(standing(0)).mOutcome, Outcome::Ahead);
            EXPECT_TRUE(window.take(standing(0)).mOpened);
            EXPECT_EQ(window.getSeen(), 6u);

            MeasureWindow played(2, 30, true);
            takeAll(played, { standing(0), standing(0), paused() });
            EXPECT_TRUE(played.take(standing(0)).mOpened) << "a played pause runs the warm-up";
            EXPECT_EQ(played.getWarmedPaused(), 0u);
        }

        /// **The wait fails after `sStallMs` of frames that brought no cell nearer.** At a second a
        /// frame: the first frame sets the fewest cells, and ten more bring none, which is 10 s and
        /// not past it. The twelfth is past it. A frame that leaves fewer cells starts the count over.
        /// A played session never stalls.
        TEST(RtxMeasureWindowTest, aWorldThatStopsArrivingFailsTheWaitPastTheStall)
        {
            MeasureWindow window(0, 30, false);
            const std::vector<Outcome> waiting = takeAll(window, std::vector<WindowFrame>(11, standing(5, 1000.0)));
            EXPECT_EQ(waiting, std::vector<Outcome>(11, Outcome::Ahead));
            EXPECT_DOUBLE_EQ(window.getStalledMs(), sStallMs);
            EXPECT_EQ(window.take(standing(5, 1000.0)).mOutcome, Outcome::Stalled);
            EXPECT_EQ(window.getLeastToStand(), 5u);

            MeasureWindow arriving(0, 30, false);
            takeAll(arriving, std::vector<WindowFrame>(10, standing(5, 1000.0)));
            arriving.take(standing(4, 1000.0));
            EXPECT_DOUBLE_EQ(arriving.getStalledMs(), 0.0) << "a cell nearer starts the count over";

            MeasureWindow played(0, 30, true);
            EXPECT_EQ(takeAll(played, std::vector<WindowFrame>(30, standing(5, 1000.0))).back(), Outcome::Ahead);
        }

        /// A pause ahead of the measurement fails the stop on the frame past its limit: a limit of
        /// three fails the fourth paused frame.
        TEST(RtxMeasureWindowTest, aWorldPausedPastTheLimitFailsTheStop)
        {
            MeasureWindow window(0, 3, false);
            EXPECT_EQ(takeAll(window, { paused(), paused(), paused() }), std::vector<Outcome>(3, Outcome::Ahead));
            EXPECT_EQ(window.take(paused()).mOutcome, Outcome::PausedTooLong);
        }

        /// **A frame's wall time closes the span the frame before it worked in.** The closed span
        /// holds the frame before's walk and trace and the meshes it brought, beside this frame's
        /// frame, update and sleep.
        TEST(RtxMeasureWindowTest, aFrameTimeClosesTheSpanTheFrameBeforeWorkedIn)
        {
            MeasureWindow window(0, 30, false);

            WindowFrame before = standing(0, 16.0);
            before.mSpend.at(Rtx::Timing::Walk) = 3.0;
            before.mSpend.at(Rtx::Timing::Trace) = 7.0;
            before.mSpend.at(Rtx::Timing::Update) = 1.0;
            before.mArrivedMeshes = 4;
            const MeasureWindow::Taken first = window.take(before);
            EXPECT_EQ(first.mClosedArrived, 0u) << "nothing before the first frame";

            WindowFrame after = standing(0, 20.0);
            after.mSpend.at(Rtx::Timing::Walk) = 9.0;
            after.mSpend.at(Rtx::Timing::Update) = 2.0;
            after.mSpend.at(Rtx::Timing::Sleep) = 0.5;
            const MeasureWindow::Taken second = window.take(after);
            EXPECT_EQ(second.mClosed.at(Rtx::Timing::Frame), 20.0);
            EXPECT_EQ(second.mClosed.at(Rtx::Timing::Update), 2.0);
            EXPECT_EQ(second.mClosed.at(Rtx::Timing::Sleep), 0.5);
            EXPECT_EQ(second.mClosed.at(Rtx::Timing::Walk), 3.0) << "the frame before's walk";
            EXPECT_EQ(second.mClosed.at(Rtx::Timing::Trace), 7.0) << "the frame before's trace";
            EXPECT_EQ(second.mClosedArrived, 4u);
        }
    }
}
