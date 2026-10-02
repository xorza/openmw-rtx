#include <gtest/gtest.h>

#include <apps/rtxtool/model/benchrun.hpp>
#include <apps/rtxtool/model/runrecord.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A run's status tells "differed" from "failed"**: a run that only differed from its
        /// reference answers `sDifferedStatus`, one that failed answers 1 whatever else differed, and
        /// a failed check is a failure.
        TEST(RtxRunRecordTest, aRunThatDifferedIsToldApartFromOneThatFailed)
        {
            EXPECT_EQ(RunRecord().describe(nullptr).mExitStatus, 0);

            RunRecord differed;
            differed.differ();
            EXPECT_EQ(differed.describe(nullptr).mExitStatus, sDifferedStatus);

            RunRecord both;
            both.differ();
            both.fail();
            EXPECT_EQ(both.describe(nullptr).mExitStatus, 1);

            RunRecord checked;
            checked.checked(true);
            EXPECT_EQ(checked.describe(nullptr).mExitStatus, 0);
            checked.checked(false);
            EXPECT_EQ(checked.describe(nullptr).mExitStatus, 1);
        }
    }
}
