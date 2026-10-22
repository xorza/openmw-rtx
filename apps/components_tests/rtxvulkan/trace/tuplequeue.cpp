#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtxvulkan/trace/tuplequeue.hpp>

namespace Rtx
{
    namespace
    {
        /// **Every item is claimed once, the runs in order, and a tuple is made by its last item.**
        /// Three tuples of five indices, run as 3, 0 and 1 with two, three and one items: items 0–1
        /// are tuple 3's, 2–4 tuple 0's and 5 tuple 1's. Claimed with no tuple wanted, they come in
        /// that order; landing tuple 0's three says made on the third alone.
        TEST(RtxTupleQueueTest, everyItemIsClaimedOnceInTheRunsOrder)
        {
            const std::array runs{ TupleRun{ .mTuple = 3, .mItems = 2 }, TupleRun{ .mTuple = 0, .mItems = 3 },
                TupleRun{ .mTuple = 1, .mItems = 1 } };
            TupleQueue queue(runs, 5);
            ASSERT_EQ(queue.getItemCount(), 6u);

            std::vector<std::uint32_t> claimed;
            for (std::uint32_t turn = 0; turn < queue.getItemCount(); ++turn)
                claimed.push_back(queue.claim(5));
            EXPECT_EQ(claimed, (std::vector<std::uint32_t>{ 0, 1, 2, 3, 4, 5 }));

            EXPECT_FALSE(queue.land(0));
            EXPECT_FALSE(queue.land(0));
            EXPECT_TRUE(queue.land(0)) << "tuple 0's third item is its last";
            EXPECT_TRUE(queue.land(1)) << "tuple 1 is one item";
        }

        /// **A wanted tuple is claimed first**, and the rest go on in order once it is: with tuple 1
        /// (items 5) wanted from the start, then tuple 0 (items 2–4) wanted after one claim, the
        /// claims come 5, then 2, 3, 4, then the runs' order resumes at 0, 1. A tuple with no run,
        /// 4, wants nothing.
        TEST(RtxTupleQueueTest, aWantedTupleIsClaimedFirst)
        {
            const std::array runs{ TupleRun{ .mTuple = 3, .mItems = 2 }, TupleRun{ .mTuple = 0, .mItems = 3 },
                TupleRun{ .mTuple = 1, .mItems = 1 } };
            TupleQueue queue(runs, 5);

            std::vector<std::uint32_t> claimed;
            claimed.push_back(queue.claim(1));
            for (int turn = 0; turn < 3; ++turn)
                claimed.push_back(queue.claim(0));
            claimed.push_back(queue.claim(4));
            claimed.push_back(queue.claim(0));
            EXPECT_EQ(claimed, (std::vector<std::uint32_t>{ 5, 2, 3, 4, 0, 1 }));

            Testing::expectDies([&] { queue.claim(5); }, "a claim on a tuple queue found no item left");
        }
    }
}
