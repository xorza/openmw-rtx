#include <cstdint>
#include <unordered_map>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/mirror/mirroridentity.hpp>
#include <components/rtx/mirror/mirrorpass.hpp>

namespace Rtx
{
    namespace
    {
        using Map = std::unordered_map<int, Known>;

        struct RtxKeptTest : testing::Test
        {
            MirrorPass mPass;
            Kept<Map> mKept{ mPass };

            /// What the extractor does between two walks: the sweep, then the epoch.
            void nextEpoch() { ++mPass.mEpoch; }
        };

        /// **A held entry survives a sweep whatever its stamp, and counts as reached for `whole`.**
        /// The cell ring stands models the walk never meets, so without this the sweep after every
        /// frame would take them and the count would call for a sweep on every frame besides.
        TEST_F(RtxKeptTest, aHeldEntrySurvivesTheSweepAndIsWholeUnstamped)
        {
            mKept.add(1, Known{ .mIndex = 10 });
            mKept.add(2, Known{ .mIndex = 20 });
            EXPECT_TRUE(mKept.whole()) << "two entries, both stamped by the epoch that added them";

            mKept.hold(mKept.find(2));
            EXPECT_TRUE(mKept.whole()) << "a hold on a stamped entry changes nothing this epoch";

            nextEpoch();
            EXPECT_FALSE(mKept.whole()) << "the unheld entry is unreached in the new epoch";

            mKept.stamp(mKept.find(1));
            EXPECT_TRUE(mKept.whole()) << "one reached and one held is the whole map";

            // A stamp on a held entry is not counted twice.
            mKept.stamp(mKept.find(2));
            EXPECT_TRUE(mKept.whole());

            nextEpoch();
            mKept.stamp(mKept.find(1));
            EXPECT_EQ(mKept.retire(), 0u) << "nothing to drop: one stamped, one held";
            EXPECT_NE(mKept.find(2), mKept.end()) << "the held entry is a survivor";
        }

        /// **A dropped hold is a sweep owed**, even on an epoch that reached everything else: the
        /// entry is stale the moment nothing holds it, and `whole` has to say so or the row it names
        /// leaks until something else dies.
        TEST_F(RtxKeptTest, droppingTheLastHoldOwesASweepThatTakesTheEntry)
        {
            mKept.add(1, Known{ .mIndex = 10 });
            mKept.hold(mKept.find(1));
            mKept.hold(mKept.find(1));

            nextEpoch();
            EXPECT_TRUE(mKept.whole()) << "a held map with nothing unheld in it is whole before any stamp";

            mKept.drop(mKept.find(1));
            EXPECT_TRUE(mKept.whole()) << "one hold of two given back keeps the entry held";

            mKept.drop(mKept.find(1));
            EXPECT_FALSE(mKept.whole()) << "the last hold went and the entry carries an old stamp";

            std::uint32_t dropped = 0;
            mKept.retire([&](const Known& gone) {
                EXPECT_EQ(gone.mIndex, 10u);
                ++dropped;
            });
            EXPECT_EQ(dropped, 1u);
            EXPECT_TRUE(mKept.whole()) << "an empty map is whole";

            // The other way round: a hold dropped on an entry the epoch stamped is a reached entry
            // again, and owes nothing.
            mKept.add(2, Known{ .mIndex = 20 });
            mKept.hold(mKept.find(2));
            mKept.drop(mKept.find(2));
            EXPECT_TRUE(mKept.whole());
            EXPECT_EQ(mKept.retire(), 0u);
            EXPECT_NE(mKept.find(2), mKept.end());
        }

        /// An entry abandoned mid-walk comes off the reached count and owes a sweep, or every
        /// sweep after would find the map one short of whole for ever. A held one is not the
        /// walk's to abandon: its holders would release a key the map no longer knows.
        TEST_F(RtxKeptTest, abandoningAnEntryCountsItOffAndAHeldOneIsNotAbandoned)
        {
            mKept.add(1, Known{ .mIndex = 10 });
            mKept.add(2, Known{ .mIndex = 20 });
            mKept.hold(mKept.find(2));

            mKept.abandon(mKept.find(1));
            EXPECT_FALSE(mKept.whole()) << "an abandon owes a sweep";

            EXPECT_EQ(mKept.retire(), 0u) << "the abandoned entry is already gone";
            EXPECT_EQ(mKept.find(1), mKept.end());
            EXPECT_NE(mKept.find(2), mKept.end());
            EXPECT_TRUE(mKept.whole());

            nextEpoch();
            EXPECT_TRUE(mKept.whole()) << "the held entry is the whole map";

            Testing::expectAssertDies(
                [&] { mKept.abandon(mKept.find(2)); }, "an entry abandoned while something holds it");
        }

        /// **A retire that skips because the counters say whole checks that no entry is stale.** An
        /// epoch written past `stamp` is a drift the counters cannot see: entry 1 counts as reached
        /// while its epoch says it is not, so the map reads whole with a stale entry in it.
        TEST_F(RtxKeptTest, aSkippedRetireAssertsThatNothingWasStale)
        {
            mKept.add(1, Known{ .mIndex = 10 });
            const std::uint64_t added = mKept.find(1)->second.mReach.mEpoch;
            nextEpoch();
            EXPECT_FALSE(mKept.whole());

            mKept.stamp(mKept.find(1));
            mKept.find(1)->second.mReach.mEpoch = added;
            ASSERT_TRUE(mKept.whole()) << "the drift this test is made of";

            Testing::expectAssertDies(
                [&] { mKept.retire(); }, "a map counted whole with an entry neither the epoch nor a hold keeps");
        }
    }
}
