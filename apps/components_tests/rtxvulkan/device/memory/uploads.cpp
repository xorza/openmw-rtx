#include <array>
#include <vector>

#include <gtest/gtest.h>

#include <components/rtx/common/index.hpp>
#include <components/rtxvulkan/device/memory/batch.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>

namespace Rtx
{
    namespace
    {
        /// **A debt's rows become one run a stretch of adjacent rows**, so a staged sync is one copy
        /// region a stretch and not one a row: rows 1, 2 and 3 of sixteen bytes are bytes 16 to 64,
        /// row 7 bytes 112 to 128, and rows 9 and 10 bytes 144 to 176 — from where they lie in the
        /// table to where they lie in its copy.
        TEST(RtxUploadsTest, adjacentRowsAreOneRun)
        {
            constexpr std::array<Index, 6> rows{ 1, 2, 3, 7, 9, 10 };
            std::vector<CopyRun> runs;
            appendRowRuns(rows, 16, runs);
            EXPECT_EQ(runs,
                (std::vector<CopyRun>{ CopyRun{ .mFrom = 16, .mTo = 16, .mSize = 48 },
                    CopyRun{ .mFrom = 112, .mTo = 112, .mSize = 16 },
                    CopyRun{ .mFrom = 144, .mTo = 144, .mSize = 32 } }));

            // Appended to what is there, joining the last run where the next row carries it on.
            appendRowRuns(std::array<Index, 2>{ 11, 13 }, 16, runs);
            ASSERT_EQ(runs.size(), 4u);
            EXPECT_EQ(runs[2], (CopyRun{ .mFrom = 144, .mTo = 144, .mSize = 48 })) << "row 11 carries rows 9 and 10 on";
            EXPECT_EQ(runs[3], (CopyRun{ .mFrom = 208, .mTo = 208, .mSize = 16 }));

            // No rows, no runs.
            std::vector<CopyRun> none;
            appendRowRuns({}, 16, none);
            EXPECT_TRUE(none.empty());
        }

        /// **A run joins the last only where it carries that run on at both ends**, since a region
        /// copies one stretch from one offset to one offset; and a run of no bytes is no run, since a
        /// copy region of nought bytes is invalid.
        TEST(RtxUploadsTest, aRunJoinsTheLastOnlyWhereBothEndsCarryOn)
        {
            std::vector<CopyRun> runs;
            appendRun(runs, CopyRun{ .mFrom = 0, .mTo = 100, .mSize = 8 });
            appendRun(runs, CopyRun{ .mFrom = 8, .mTo = 200, .mSize = 8 });
            appendRun(runs, CopyRun{ .mFrom = 40, .mTo = 208, .mSize = 4 });
            appendRun(runs, CopyRun{ .mFrom = 16, .mTo = 208, .mSize = 4 });
            appendRun(runs, CopyRun{ .mFrom = 20, .mTo = 212, .mSize = 0 });

            EXPECT_EQ(runs,
                (std::vector<CopyRun>{ CopyRun{ .mFrom = 0, .mTo = 100, .mSize = 8 },
                    CopyRun{ .mFrom = 8, .mTo = 200, .mSize = 8 }, CopyRun{ .mFrom = 40, .mTo = 208, .mSize = 4 },
                    CopyRun{ .mFrom = 16, .mTo = 208, .mSize = 4 } }))
                << "the source carried on but the destination did not, then the reverse, then nothing";

            appendRun(runs, CopyRun{ .mFrom = 20, .mTo = 212, .mSize = 4 });
            EXPECT_EQ(runs.back(), (CopyRun{ .mFrom = 16, .mTo = 208, .mSize = 8 })) << "both ends carried on";
        }
    }
}
