#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <boost/container/flat_set.hpp>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/slots.hpp>

namespace Rtx
{
    namespace
    {
        /// The rows of the tables these tests stand up, a table of their own.
        using Row = RowIndex<struct TestRows>;

        /// `at` as rows, in order: what a list of slots is held against.
        std::vector<Row> rowsAt(std::initializer_list<std::uint32_t> at)
        {
            std::vector<Row> rows;
            for (const std::uint32_t one : at)
                rows.emplace_back(one);
            return rows;
        }

        /// A table that hands its rows' protected half to the test, as a table's own `add` and drop
        /// call it.
        struct HeldInts : HeldRows<std::int32_t, Row>
        {
            using HeldRows::dropRow;
            using HeldRows::freeRow;
            using HeldRows::holdRow;
            using HeldRows::takeRow;
        };

        /// **A row lives by its holds: the last drop is where it goes, and a freed slot is taken
        /// over with none.** What every table counts its holders by, so it is proved once here: a
        /// texture held by materials, a rig stood on by meshes, a mesh by its identity and its
        /// placements. The freed count moves by one a free and never otherwise, which is what a
        /// retire reports what went by.
        TEST(RtxSlotRowsTest, aRowIsFreedByTheDropAfterWhichNothingHoldsIt)
        {
            HeldInts rows;
            const Row first = rows.takeRow(1);
            const Row second = rows.takeRow(2);
            ASSERT_EQ(rows.getLiveCount(), 2u);
            EXPECT_EQ(rows.getHolds(second), 0u) << "a row arrives with no holds";

            // **A row's tier is the best any hold on it states**: kept, then shown over it, then kept again once the
            // shown hold goes, and unheld with none.
            EXPECT_EQ(rows.getTier(second), HoldTier::Unheld);
            rows.holdRow(second, HoldTier::Kept);
            EXPECT_EQ(rows.getTier(second), HoldTier::Kept);
            rows.holdRow(second, HoldTier::Shown);
            EXPECT_EQ(rows.getHolds(second), 2u);
            EXPECT_EQ(rows.getTier(second), HoldTier::Shown);

            // One hold back is still held; the last is where the table frees the row.
            EXPECT_FALSE(rows.dropRow(second, HoldTier::Shown));
            EXPECT_EQ(rows.getTier(second), HoldTier::Kept);
            EXPECT_TRUE(rows.dropRow(second, HoldTier::Kept));
            EXPECT_EQ(rows.getTier(second), HoldTier::Unheld);
            EXPECT_EQ(rows.getFreedCount(), 0u) << "the table frees, not the drop";
            const std::uint32_t tenant = rows.generationOf(second);
            rows.freeRow(second);
            EXPECT_EQ(rows.generationOf(second), tenant + 1) << "a slot freed is another tenant's next";
            EXPECT_EQ(rows.generationOf(first), 0u);
            EXPECT_EQ(rows.getFreedCount(), 1u);
            EXPECT_EQ(rows.getLiveCount(), 1u);
            EXPECT_FALSE(rows.isLive(second));
            EXPECT_TRUE(rows.isLive(first));

            // A freed slot is taken over with no holds, whatever its last tenant carried.
            const Row again = rows.takeRow(4);
            EXPECT_EQ(again, second) << "the lowest free slot";
            EXPECT_EQ(rows.getHolds(again), 0u);

            rows.holdRow(first, HoldTier::Shown);
            Testing::expectAssertDies([&] { rows.dropRow(first, HoldTier::Kept); },
                "a slot given back at a tier more often than it was held at it");
            Testing::expectAssertDies([&] { rows.holdRow(first, HoldTier::Unheld); }, "a hold that states no tier");
            Testing::expectAssertDies([&] { rows.freeRow(first); }, "a slot freed while something holds it");
        }

        struct Keyed
        {
            std::int32_t mKey = 0;
            std::int32_t mValue = 0;
        };

        struct KeyOfKeyed
        {
            std::int32_t operator()(const Keyed& row) const { return row.mKey; }
        };

        /// **One comparator orders the rows and finds them by the key alone**, so a `flat_set` of
        /// rows keyed by a field inside each is searched with no row made up to search with. Rows
        /// on both sides, a key on either, and a key against a key: the four shapes a set asks.
        TEST(RtxKeyedLessTest, rowsAreOrderedAndFoundByTheKeyInsideThem)
        {
            boost::container::flat_set<Keyed, KeyedLess<std::int32_t, KeyOfKeyed>, std::vector<Keyed>> rows;
            for (const std::int32_t key : { 3, 1, 5, 4, 2 })
                rows.insert(Keyed{ .mKey = key, .mValue = key * 10 });

            ASSERT_EQ(rows.size(), 5u);
            std::int32_t expected = 1;
            for (const Keyed& row : rows)
                EXPECT_EQ(row.mKey, expected++) << "the rows are not in the key's order";

            ASSERT_NE(rows.find(4), rows.end());
            EXPECT_EQ(rows.find(4)->mValue, 40);
            EXPECT_EQ(rows.find(6), rows.end());
            EXPECT_TRUE(rows.contains(2));

            // A row already keyed is refused, whatever else it carries.
            EXPECT_FALSE(rows.insert(Keyed{ .mKey = 3, .mValue = 0 }).second);
            EXPECT_EQ(rows.find(3)->mValue, 30);

            // The rows are mutable in place, which is what the ring needs of its cells.
            rows.find(5)->mValue = 55;
            EXPECT_EQ(rows.find(5)->mValue, 55);
        }

        /// **The pool knows which slots it holds.** A slot freed is on the list, a slot taken is
        /// not, and the two questions every table asks of it read the same byte.
        TEST(RtxSlotPoolTest, aSlotIsFreeFromItsFreeToItsTake)
        {
            SlotPool<Row> pool;
            EXPECT_FALSE(pool.isFree(Row(3))) << "a slot nothing ever freed";
            EXPECT_EQ(pool.take(), sNoRow);

            pool.free(Row(3));
            pool.free(Row(1));
            EXPECT_TRUE(pool.isFree(Row(3)));
            EXPECT_TRUE(pool.isFree(Row(1)));
            EXPECT_FALSE(pool.isFree(Row(2)));

            EXPECT_EQ(pool.take(), Row(1)) << "the lowest";
            EXPECT_FALSE(pool.isFree(Row(1)));
            EXPECT_TRUE(pool.isFree(Row(3)));
            EXPECT_EQ(pool.take(), Row(3));
            EXPECT_FALSE(pool.isFree(Row(3)));
            EXPECT_EQ(pool.take(), sNoRow);
        }

        /// **A slot freed twice is the pool's assert**, and not two entries on the heap handing one
        /// slot to two takers — which `VulkanRenderer::dropViewScene` could reach with a drop of a
        /// slot already dropped.
        TEST(RtxSlotPoolTest, aSlotFreedTwiceDies)
        {
            SlotPool<Row> pool;
            pool.free(Row(2));
            Testing::expectAssertDies([&] { pool.free(Row(2)); }, "a slot freed twice");
        }

        /// **A slot taken out and named again before the compact stands in the list once, where it
        /// stood.** What lets a table take a slot out at a drop and settle its lists once a sweep is
        /// done: a fade counts a row out and back in, and a mesh slot freed and taken again is posed
        /// again inside one sweep. Removed twice is removed once, and the compact drops exactly the
        /// slots still out — 7 and 5 here, with 3 put back and 9 never touched.
        TEST(RtxSlotSetTest, aSlotNamedAgainBeforeTheCompactIsListedOnceWhereItStood)
        {
            SlotSet<Row> set;
            set.grow(10);
            for (const Row slot : rowsAt({ 3, 7, 5, 9 }))
                set.add(slot);

            set.remove(Row(3));
            set.remove(Row(7));
            set.remove(Row(7));
            set.remove(Row(5));
            set.remove(Row(2));
            EXPECT_FALSE(set.has(Row(3))) << "the flag is exact before the compact";
            EXPECT_FALSE(set.has(Row(2)));
            Testing::expectAssertDies([&] { (void)set.getSlots(); }, "between a remove and the compact");

            set.add(Row(3));
            set.add(Row(3));
            EXPECT_TRUE(set.has(Row(3)));
            set.compact();
            EXPECT_EQ(std::vector<Row>(set.getSlots().begin(), set.getSlots().end()), rowsAt({ 3, 9 }));
            EXPECT_FALSE(set.has(Row(7)));

            // A slot compacted away is absent, so naming it again appends it.
            set.add(Row(7));
            set.remove(Row(9));
            set.add(Row(9));
            EXPECT_EQ(std::vector<Row>(set.getSlots().begin(), set.getSlots().end()), rowsAt({ 3, 9, 7 }))
                << "a slot put back before the compact needs none";

            set.clear();
            EXPECT_TRUE(set.empty());
            EXPECT_FALSE(set.has(Row(3)));
            set.add(Row(9));
            EXPECT_EQ(set.getSlots().size(), 1u) << "a cleared set kept a flag";
        }

        /// **The last word wins, and a slot moved between the two lists is settled by one compact.**
        /// A texture that arrived and went inside one hand-over is reported gone, and one that went
        /// and was taken over is reported arrived — each once, however often it changed sides.
        TEST(RtxSlotChangesTest, theLastWordWinsAndOneCompactSettlesBothLists)
        {
            SlotChanges<Row> changes;
            changes.grow(8);
            changes.note(Row(1), SlotNews::Arrived);
            changes.note(Row(2), SlotNews::Arrived);
            changes.note(Row(4), SlotNews::Freed);
            changes.note(Row(1), SlotNews::Freed);
            changes.note(Row(4), SlotNews::Arrived);
            changes.note(Row(4), SlotNews::Freed);
            changes.note(Row(4), SlotNews::Arrived);
            changes.compact();

            EXPECT_EQ(std::vector<Row>(changes.getArrived().begin(), changes.getArrived().end()), rowsAt({ 2, 4 }));
            EXPECT_EQ(std::vector<Row>(changes.getFreed().begin(), changes.getFreed().end()), rowsAt({ 1 }));
        }

    }
}
