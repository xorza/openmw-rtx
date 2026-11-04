#include <gtest/gtest.h>

#include <components/rtxvulkan/scene/bottomlevelstore.hpp>

namespace Rtx
{
    namespace
    {
        /// **A backlog holds what is outstanding and no more**: asked one question a pass and
        /// answering the one before, it holds two at the most after each settle, where it held every
        /// question asked until a pass drained it — which a route that builds every frame never
        /// reaches. What is left keeps its order.
        TEST(RtxBacklogTest, aBacklogHoldsWhatIsOutstandingAndNoMore)
        {
            Backlog<int> backlog;
            backlog.push(0);
            for (int pass = 1; pass <= 1000; ++pass)
            {
                backlog.push(pass);
                ASSERT_EQ(backlog.at(0), pass - 1);
                backlog.pop(1);
                backlog.settle();
                ASSERT_EQ(backlog.mItems.size(), 1u) << "pass " << pass;
                ASSERT_EQ(backlog.at(0), pass);
            }
            backlog.pop(1);
            backlog.settle();
            EXPECT_TRUE(backlog.mItems.empty());
        }
    }
}
