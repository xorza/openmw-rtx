#include <gtest/gtest.h>

#include <components/rtx/shaders/accumulate.h>

namespace Rtx
{
    namespace
    {
        /// **How far the slow mean is moved toward the fast one, by hand.** `antilagShare(slow, fast,
        /// low, high)` with the box `[low, high]` grown to hold `fast`:
        ///
        /// - Inside the box, nothing: 0.5 in `[0.4, 0.7]`.
        /// - Above it, to its top: 1.0 toward 0.2 in `[0.1, 0.4]` stops at 0.4, `(0.4 - 1.0) / (0.2 -
        ///   1.0) = 0.75` of the way.
        /// - Below a box the fast mean grew: 0.0 toward 0.9 in `[0.4, 0.6]`, grown to `[0.4, 0.9]`,
        ///   stops at 0.4, `0.4 / 0.9` of the way.
        /// - Below a box the fast mean stands under: 0.0 toward 0.3 in `[0.5, 0.6]`, grown to
        ///   `[0.3, 0.6]`, stops at 0.3, the whole way, and never past it.
        /// - Where the two means agree, nothing, whatever the box.
        TEST(RtxAccumulateClampTest, theSlowMeanMovesToTheEdgeOfTheFastMeansBox)
        {
            EXPECT_EQ(Shaders::antilagShare(0.5f, 0.6f, 0.4f, 0.7f), 0.0f);
            EXPECT_FLOAT_EQ(Shaders::antilagShare(1.0f, 0.2f, 0.1f, 0.4f), 0.75f);
            EXPECT_FLOAT_EQ(Shaders::antilagShare(0.0f, 0.9f, 0.4f, 0.6f), 0.4f / 0.9f);
            EXPECT_EQ(Shaders::antilagShare(0.0f, 0.3f, 0.5f, 0.6f), 1.0f);
            EXPECT_EQ(Shaders::antilagShare(0.25f, 0.25f, 0.5f, 0.6f), 0.0f);
        }

        /// **How far both means are pushed on toward the samples, by hand**, at
        /// `ACCUMULATE_ACCELERATION` = 3: `3 × share × gap` along a way of `distance`.
        ///
        /// - A gap of 0.2 the clamp moved half of, a way of 1: `3 × 0.5 × 0.2 = 0.3` of it.
        /// - A gap of 1 it moved all of, a way of 0.5: 6, held to the whole way and never past it.
        /// - Nothing where the clamp moved nothing, and nothing where the samples stand at the fast mean.
        TEST(RtxAccumulateClampTest, bothMeansArePushedTowardTheSamplesByWhatTheClampMoved)
        {
            EXPECT_FLOAT_EQ(Shaders::antilagAcceleration(0.2f, 0.5f, 1.0f), 0.3f);
            EXPECT_EQ(Shaders::antilagAcceleration(1.0f, 1.0f, 0.5f), 1.0f);
            EXPECT_EQ(Shaders::antilagAcceleration(0.2f, 0.0f, 1.0f), 0.0f);
            EXPECT_EQ(Shaders::antilagAcceleration(0.2f, 0.5f, 0.0f), 0.0f);
        }
    }
}
