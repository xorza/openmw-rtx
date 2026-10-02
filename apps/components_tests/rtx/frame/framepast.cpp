#include <gtest/gtest.h>

#include <components/rtx/frame/framepast.hpp>

namespace Rtx
{
    namespace
    {
        /// **One row per event, each stating all three columns**: what the host's loss, a new
        /// extent and a new world cost the reprojection, the eye and the water's wake. A cut keeps
        /// the wake, as the rasterizer keeps its ripples over a teleport; a new extent keeps the
        /// eye, so a mode changed in the menu does not snap the brightness.
        TEST(RtxFramePastTest, eachEventCostsItsOwnColumns)
        {
            struct Row
            {
                FramePast mPast;
                bool mReprojection;
                bool mEye;
                bool mWater;
                const char* mWhat;
            };
            const Row rows[] = {
                { FramePast::of(HistoryLoss::None), false, false, false, "a frame that follows the last" },
                { FramePast::of(HistoryLoss::Cut), true, true, false, "a cut" },
                { FramePast::of(HistoryLoss::Worldspace), true, true, true, "another worldspace" },
                { FramePast::resized(), true, false, false, "a new extent" },
                { FramePast::everything(), true, true, true, "a new world" },
                { FramePast{}, false, false, false, "nothing" },
            };

            for (const Row& row : rows)
            {
                EXPECT_EQ(row.mPast.mReprojectionLost, row.mReprojection) << row.mWhat;
                EXPECT_EQ(row.mPast.mEyeLost, row.mEye) << row.mWhat;
                EXPECT_EQ(row.mPast.mWaterLost, row.mWater) << row.mWhat;
            }
        }

        /// Two events before one frame cost what either costs: a resize and a cut lose the eye, and
        /// a cut and another worldspace lose the wake. What was lost stays lost.
        TEST(RtxFramePastTest, eventsBeforeOneFrameCostWhatEitherCosts)
        {
            FramePast past = FramePast::resized();
            past |= FramePast::of(HistoryLoss::Cut);
            EXPECT_EQ(past, FramePast::of(HistoryLoss::Cut));

            past |= FramePast::of(HistoryLoss::Worldspace);
            EXPECT_EQ(past, FramePast::everything());

            past |= FramePast::of(HistoryLoss::None);
            EXPECT_EQ(past, FramePast::everything()) << "a frame with nothing to say gave a loss back";
        }
    }
}
