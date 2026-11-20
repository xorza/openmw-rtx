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
            outcomes.reserve(frames.size());
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

            EXPECT_EQ(window.getSinceWhole(), std::nullopt);
            EXPECT_EQ(
                takeAll(window, { standing(5), standing(3), standing(3) }), std::vector<Outcome>(3, Outcome::Ahead));
            EXPECT_EQ(window.getSinceWhole(), std::nullopt) << "three frames short of the world";
            EXPECT_EQ(window.take(standing(0)).mOutcome, Outcome::Ahead);
            EXPECT_EQ(window.getSinceWhole(), 0u) << "the fourth frame drew it: the fifth is the stop's first";
            const std::vector<Outcome> ahead = takeAll(window, { standing(0) });
            EXPECT_EQ(window.getSinceWhole(), 1u);
            EXPECT_EQ(ahead, std::vector<Outcome>(1, Outcome::Ahead));
            EXPECT_EQ(window.getWaited(), 4u) << "the wait counts the frame that stood whole";
            EXPECT_EQ(window.getMeasuredIndex(), std::nullopt) << "one frame of the warm-up is still to run";
            EXPECT_EQ(window.getMeasuredFrom(), std::nullopt);

            EXPECT_EQ(window.take(standing(0)).mOutcome, Outcome::Ahead);
            EXPECT_EQ(window.getMeasuredIndex(), 0u) << "the frame about to be taken opens the measurement";

            const MeasureWindow::Taken first = window.take(standing(0));
            EXPECT_TRUE(first.mOpened);
            EXPECT_EQ(first.mOutcome, Outcome::Measured);
            EXPECT_EQ(first.mDrawn, 1u);
            EXPECT_EQ(window.getSeen(), 7u);
            EXPECT_EQ(window.getMeasuredFrom(), 6u) << "the seventh frame, traced at six seen";
            EXPECT_EQ(window.getSinceWhole(), 3u) << "seven seen, four of them the wait";

            const MeasureWindow::Taken second = window.take(standing(0));
            EXPECT_FALSE(second.mOpened);
            EXPECT_EQ(second.mDrawn, 2u);
            EXPECT_EQ(window.getMeasuredIndex(), 2u);
        }

        /// **A flight that goes on from the stop before is measured from its second frame**, whole or not: its world
        /// is the one that stop flew through (`WorldStands::Already`), and its first frame's wall time holds what ran
        /// between the two stops. Awaited, the same frames open nothing until one stands whole.
        TEST(RtxMeasureWindowTest, aWorldStandingAlreadyIsMeasuredFromItsSecondFrame)
        {
            MeasureWindow going(0, 30, false, WorldStands::Already);
            EXPECT_EQ(going.getSinceWhole(), 0u) << "a world that stood stood from before the first frame";
            EXPECT_EQ(going.getMeasuredIndex(), std::nullopt) << "the frame the stops change on is ahead";
            EXPECT_EQ(going.getWarmup(), 1u);
            const MeasureWindow::Taken changing = going.take(standing(5));
            EXPECT_FALSE(changing.mOpened);
            EXPECT_EQ(changing.mOutcome, Outcome::Ahead);
            EXPECT_EQ(going.getMeasuredIndex(), 0u);
            const MeasureWindow::Taken first = going.take(standing(5));
            EXPECT_TRUE(first.mOpened);
            EXPECT_EQ(first.mOutcome, Outcome::Measured);
            EXPECT_EQ(going.getMeasuredFrom(), 1u) << "the second frame, traced at one seen";
            EXPECT_EQ(going.getSinceWhole(), 2u);
            EXPECT_EQ(going.take(standing(3)).mOutcome, Outcome::Measured);

            MeasureWindow awaited(0, 30, false);
            EXPECT_EQ(takeAll(awaited, { standing(5), standing(3) }), std::vector<Outcome>(2, Outcome::Ahead));
            EXPECT_EQ(awaited.getSinceWhole(), std::nullopt);
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

            // **A pause in the wait is no frame of the stop's own time either**: the frame after the one that drew the
            // world is its first, numbered nought, however many paused frames the wait held.
            MeasureWindow waiting(0, 30, false);
            WindowFrame pausedShort = standing(3);
            pausedShort.mPaused = true;
            takeAll(waiting, { standing(5), pausedShort, pausedShort, standing(0) });
            EXPECT_EQ(waiting.getWaited(), 2u) << "the paused frames are no part of the wait";
            EXPECT_EQ(waiting.getStoodAt(), 4u);
            EXPECT_EQ(waiting.getSinceWhole(), 0u);

            MeasureWindow played(2, 30, true);
            takeAll(played, { standing(0), standing(0), paused() });
            EXPECT_TRUE(played.take(standing(0)).mOpened) << "a played pause runs the warm-up";
            EXPECT_EQ(played.getWarmedPaused(), 0u);
        }

        /// **The wait fails after `sStallMs` of frames that brought no cell and no image nearer.** At a second a
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
            EXPECT_EQ(window.getUnstood(), (MeasureWindow::Unstood{ .mCells = 5, .mMissing = {}, .mArriving = false }));
            EXPECT_EQ(describeUnstood(window.getUnstood()), "5 cells short");

            MeasureWindow arriving(0, 30, false);
            takeAll(arriving, std::vector<WindowFrame>(10, standing(5, 1000.0)));
            arriving.take(standing(4, 1000.0));
            EXPECT_DOUBLE_EQ(arriving.getStalledMs(), 0.0) << "a cell nearer starts the count over";

            MeasureWindow played(0, 30, true);
            EXPECT_EQ(takeAll(played, std::vector<WindowFrame>(30, standing(5, 1000.0))).back(), Outcome::Ahead);

            // **An image nearer is the world nearer as a cell is**: every cell stood and 40 images to make, one fewer
            // a frame, never stalls over thirty seconds of frames, where counted in cells alone it stalled at the
            // twelfth. The same images standing still stall, and the failure names them by why, and what still
            // arrives; a cell nearer counts its own images from where it brought them. The images are asked of the
            // feed, but for one come and waiting for room, and a file and a composite not yet asked.
            const auto making = [](const std::uint32_t cells, const std::uint32_t images, const bool arriving) {
                WindowFrame frame = standing(cells, 1000.0);
                frame.mWhole = false;
                frame.mMissing = Rtx::MissingDemanded{
                    .mAsked = images - 3, .mArrived = 1, .mUnaskedFiles = 1, .mUnaskedComposites = 1
                };
                frame.mArriving = arriving;
                return frame;
            };
            MeasureWindow images(0, 30, false);
            for (std::uint32_t left = 40; left > 10; --left)
                ASSERT_EQ(images.take(making(0, left, true)).mOutcome, Outcome::Ahead) << left;
            EXPECT_DOUBLE_EQ(images.getStalledMs(), 0.0);
            EXPECT_EQ(takeAll(images, std::vector<WindowFrame>(10, making(0, 11, true))).back(), Outcome::Ahead);
            EXPECT_EQ(images.take(making(0, 11, true)).mOutcome, Outcome::Stalled);
            EXPECT_EQ(images.getUnstood(),
                (MeasureWindow::Unstood{ .mCells = 0,
                    .mMissing
                    = Rtx::MissingDemanded{ .mAsked = 8, .mArrived = 1, .mUnaskedFiles = 1, .mUnaskedComposites = 1 },
                    .mArriving = true }));
            EXPECT_EQ(describeUnstood(images.getUnstood()),
                "11 images still being made (8 on their way, 1 come and waiting for room, not yet asked 1 files, 0 "
                "bakes and 1 composites), content still arriving");

            MeasureWindow both(0, 30, false);
            both.take(making(3, 10, false));
            takeAll(both, std::vector<WindowFrame>(5, making(3, 10, false)));
            EXPECT_DOUBLE_EQ(both.getStalledMs(), 5000.0);
            both.take(making(2, 25, false));
            EXPECT_DOUBLE_EQ(both.getStalledMs(), 0.0) << "a cell nearer, though it brought more images";
            both.take(making(2, 24, false));
            EXPECT_DOUBLE_EQ(both.getStalledMs(), 0.0) << "an image nearer, counted from what the cell brought";
            both.take(making(2, 24, false));
            EXPECT_DOUBLE_EQ(both.getStalledMs(), 1000.0);
            EXPECT_EQ(describeUnstood(both.getUnstood()),
                "2 cells short, 24 images still being made (21 on their way, 1 come and waiting for room, not yet "
                "asked 1 files, 0 bakes and 1 composites)");

            // And a mesh standing nothing is named apart.
            EXPECT_EQ(describeUnstood(MeasureWindow::Unstood{ .mMissing = Rtx::MissingDemanded{ .mMeshes = 3 } }),
                "3 meshes still being built");
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
            EXPECT_EQ(first.mClosed.mArrived, 0u) << "nothing before the first frame";

            WindowFrame after = standing(0, 20.0);
            after.mSpend.at(Rtx::Timing::Walk) = 9.0;
            after.mSpend.at(Rtx::Timing::Update) = 2.0;
            after.mSpend.at(Rtx::Timing::Sleep) = 0.5;
            const MeasureWindow::Taken second = window.take(after);
            EXPECT_EQ(second.mClosed.mSpend.at(Rtx::Timing::Frame), 20.0);
            EXPECT_EQ(second.mClosed.mSpend.at(Rtx::Timing::Update), 2.0);
            EXPECT_EQ(second.mClosed.mSpend.at(Rtx::Timing::Sleep), 0.5);
            EXPECT_EQ(second.mClosed.mSpend.at(Rtx::Timing::Walk), 3.0) << "the frame before's walk";
            EXPECT_EQ(second.mClosed.mSpend.at(Rtx::Timing::Trace), 7.0) << "the frame before's trace";
            EXPECT_EQ(second.mClosed.mArrived, 4u);
        }

        /// **A crossing is closed with the span of the frame that first stood in the new cell**, as
        /// the worst frames are: that frame's placement is the one the read landed in. Cells A, A,
        /// B, B: the third frame crossed and rebuilt, so the fourth closes its span, 30 ms of wall
        /// and its own walk, with a rebuild. No crossing before it, and the first frame crossed
        /// nothing, having no cell before it. A crossing that appended closes as no rebuild.
        TEST(RtxMeasureWindowTest, aCrossingClosesWithTheSpanOfTheFrameThatCrossed)
        {
            const std::int32_t cells[2]{};
            MeasureWindow window(0, 30, false);
            const auto at = [&](const void* cell, double frameMs, bool rebuilt, double walkMs = 0.0) {
                WindowFrame frame = standing(0, frameMs);
                frame.mCell = cell;
                frame.mRebuilt = rebuilt;
                frame.mSpend.at(Rtx::Timing::Walk) = walkMs;
                return window.take(frame);
            };

            EXPECT_FALSE(at(&cells[0], 10.0, false).mClosedCrossing.has_value()) << "the first frame";
            EXPECT_FALSE(at(&cells[0], 10.0, false).mClosedCrossing.has_value());
            EXPECT_FALSE(at(&cells[1], 12.0, true, 8.0).mClosedCrossing.has_value())
                << "the crossing is closed by the frame after it";
            const MeasureWindow::Taken closed = at(&cells[1], 30.0, false);
            ASSERT_TRUE(closed.mClosedCrossing.has_value());
            EXPECT_TRUE(*closed.mClosedCrossing) << "the crossing's own placement rebuilt";
            EXPECT_EQ(closed.mClosed.mSpend.at(Rtx::Timing::Frame), 30.0) << "the wall the crossing's work stood in";
            EXPECT_EQ(closed.mClosed.mSpend.at(Rtx::Timing::Walk), 8.0) << "the crossing's own walk";

            EXPECT_FALSE(at(&cells[0], 10.0, false).mClosedCrossing.has_value());
            const MeasureWindow::Taken appended = at(&cells[0], 10.0, true);
            ASSERT_TRUE(appended.mClosedCrossing.has_value());
            EXPECT_FALSE(*appended.mClosedCrossing) << "the rebuild of the frame after is not the crossing's";
        }
    }
}
