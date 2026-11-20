#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/rowspan.hpp>
#include <components/rtx/common/rowvector.hpp>

namespace Rtx
{
    namespace
    {
        using Row = RowIndex<struct TestRows>;

        /// **A row made at nothing is no row, whichever way it is spelled**, and a row at any position is a row:
        /// position 0 is a row like the others, and the position past every other, `~0`, is what no row is.
        TEST(RtxRowIndexTest, aRowMadeAtNothingIsNoRowAndEveryPositionBelowTheTopIsARow)
        {
            EXPECT_TRUE(Row{}.isNone());
            EXPECT_TRUE(Row(sNoRow).isNone());
            EXPECT_EQ(Row{}, sNoRow);
            EXPECT_EQ(Row{}.at(), ~std::uint32_t{ 0 });

            EXPECT_FALSE(Row(0).isNone());
            EXPECT_NE(Row(0), sNoRow);
            EXPECT_EQ(Row(7).at(), 7u);

            // Ordered by position, so a heap of free rows hands out the lowest, and no row orders past every row.
            EXPECT_LT(Row(2), Row(3));
            EXPECT_LT(Row(3), Row(sNoRow));
        }

        /// **A span holds the rows below its length and no others**: no row is held by no span, the last
        /// position is, and the one past it is not.
        TEST(RtxRowSpanTest, aSpanHoldsTheRowsBelowItsLength)
        {
            const std::array<std::int32_t, 3> items{ 10, 20, 30 };
            const RowSpan<const std::int32_t, Row> rows{ std::span<const std::int32_t>(items) };
            EXPECT_EQ(rows.size(), 3u);
            EXPECT_TRUE(rows.holds(Row(0)));
            EXPECT_TRUE(rows.holds(Row(2)));
            EXPECT_FALSE(rows.holds(Row(3)));
            EXPECT_FALSE(rows.holds(Row(sNoRow)));
            EXPECT_EQ(rows[Row(1)], 20);
        }

        /// **A vector grows to a row and no further, and never shrinks by it**: growing to 2 makes 0, 1 and 2,
        /// the new ones at the fill; growing to a row it holds changes nothing.
        TEST(RtxRowVectorTest, aVectorGrowsToTheRowItIsAskedForAndKeepsWhatItHeld)
        {
            RowVector<std::int32_t, Row> kept;
            EXPECT_FALSE(kept.holds(Row(0)));

            kept.growTo(Row(2), -1);
            ASSERT_EQ(kept.size(), 3u);
            kept[Row(1)] = 5;
            kept.growTo(Row(1), 9);
            EXPECT_EQ(kept.size(), 3u) << "a vector grown to a row it holds grew";
            EXPECT_EQ(std::vector<std::int32_t>(kept.begin(), kept.end()), (std::vector<std::int32_t>{ -1, 5, -1 }));

            kept.growTo(Row(3));
            EXPECT_EQ(kept[Row(3)], 0) << "made as an int is";
            EXPECT_FALSE(kept.holds(Row(sNoRow)));
            EXPECT_EQ(kept.rows()[Row(1)], 5);

            // No row is the vector's assert, and not four billion rows made.
            Testing::expectAssertDies([&] { kept.growTo(Row(sNoRow)); }, "a vector grown to no row");
            Testing::expectAssertDies([&] { kept.growTo(Row(sNoRow), 1); }, "a vector grown to no row");
        }

        /// **The rows between two positions are each position from the first up to the end**, and none where
        /// the two are one.
        TEST(RtxRowRangeTest, theRowsBetweenTwoPositionsAreEachFromTheFirstUpToTheEnd)
        {
            std::vector<Row> between;
            for (const Row row : rowsBetween<Row>(2, 5))
                between.push_back(row);
            EXPECT_EQ(between, (std::vector{ Row(2), Row(3), Row(4) }));

            std::vector<Row> every;
            for (const Row row : everyRow<Row>(2))
                every.push_back(row);
            EXPECT_EQ(every, (std::vector{ Row(0), Row(1) }));

            EXPECT_TRUE(rowsBetween<Row>(4, 4).empty());
        }
    }
}
