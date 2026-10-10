#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/accumulate.h>

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

        /// **The fill the clamp leaves a channel, by hand**: a share of the bounce as held, moved from
        /// the slow mean's share toward the fast mean's by the clamp's share. A slow bounce of 1 whose
        /// fill is 0.8 holds a share of 0.8; a fast bounce of 0.4 whose fill is 0.1 one of 0.25.
        ///
        /// - Nothing moved, so the fill is the slow one to the bit: 0.3 of 0.6 held at 0.6.
        /// - A channel the box held, the luminance not moved (share 0): 0.8 of a bounce held at 0.5
        ///   is 0.4, and not the 0.8 that stood over it; 0.16 of 0.2 held at 0.35 is 0.28.
        /// - Moved the whole way: held at 0.4, the fast fill, `0.8 - 0.6 × 0.8 + 0.4 × (0.25 - 0.8) = 0.1`.
        /// - Half the way, held at 0.7: `0.7 × (0.8 + 0.25) / 2 = 0.3675`.
        /// - A slow mean of no bounce takes the fast share: 0.25 of 0.2 is 0.05. With no fast
        ///   bounce either, no fill.
        TEST(RtxAccumulateClampTest, theFillStaysTheShareOfTheBounceTheClampHeld)
        {
            EXPECT_EQ(Shaders::clampedFill(0.6f, 0.3f, 0.4f, 0.1f, 0.6f, 0.0f), 0.3f);
            EXPECT_FLOAT_EQ(Shaders::clampedFill(1.0f, 0.8f, 0.4f, 0.1f, 0.5f, 0.0f), 0.4f);
            EXPECT_FLOAT_EQ(Shaders::clampedFill(0.2f, 0.16f, 0.4f, 0.1f, 0.35f, 0.0f), 0.28f);
            EXPECT_FLOAT_EQ(Shaders::clampedFill(1.0f, 0.8f, 0.4f, 0.1f, 0.4f, 1.0f), 0.1f);
            EXPECT_FLOAT_EQ(Shaders::clampedFill(1.0f, 0.8f, 0.4f, 0.1f, 0.7f, 0.5f), 0.3675f);
            EXPECT_FLOAT_EQ(Shaders::clampedFill(0.0f, 0.0f, 0.4f, 0.1f, 0.2f, 0.5f), 0.05f);
            EXPECT_EQ(Shaders::clampedFill(0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f), 0.0f);
        }

        /// **The ring's ceiling, by hand**: 72 fast means, half at 1 and half at 3, are a mean of 2, a
        /// mean square of `(1 + 9) / 2 = 5` and a deviation of `sqrt(5 - 4) = 1`, so the ceiling at two
        /// deviations is 4.
        ///
        /// - A slow mean of 10 is held to 4, and one of 3.5 keeps all of it.
        /// - One of nought stays at nought: the ring holds a pixel down and lifts nothing.
        /// - A ring with no surface in it holds nothing down.
        TEST(RtxAccumulateClampTest, aSlowMeanIsHeldUnderItsRing)
        {
            const float sum = 36.0f * 1.0f + 36.0f * 3.0f;
            const float squares = 36.0f * 1.0f + 36.0f * 9.0f;

            EXPECT_FLOAT_EQ(Shaders::ringHeldLuminance(10.0f, sum, squares, 72.0f), 4.0f);
            EXPECT_EQ(Shaders::ringHeldLuminance(3.5f, sum, squares, 72.0f), 3.5f);
            EXPECT_EQ(Shaders::ringHeldLuminance(0.0f, sum, squares, 72.0f), 0.0f);
            EXPECT_EQ(Shaders::ringHeldLuminance(5.0f, 0.0f, 0.0f, 0.0f), 5.0f);
        }

        /// **A short history's variance is the moments' around it, raised for the shortest**: moments
        /// of one and 1.25 are a variance of `1.25 - 1²`, a quarter, which a mean of one frame doubles
        /// (`4 / 2`), one of two raises by `4 / 3` to 0.3333, and one of three or more keeps (`4 / 4` is
        /// one). The same moments at a thousand times the light, 1000 and 1.25e6, are a million times
        /// the variance: a measure in the light's own units, which a constant is not. Moments whose
        /// rounding leaves the second under the first's square are no variance and not a negative one.
        TEST(RtxAccumulateClampTest, aShortHistorysVarianceIsTheMomentsAroundIt)
        {
            EXPECT_FLOAT_EQ(Shaders::momentVariance(1.0f, 1.25f), 0.25f);
            EXPECT_FLOAT_EQ(Shaders::shortHistoryVariance(1.0f, 1.25f, 1.0f), 0.5f);
            EXPECT_FLOAT_EQ(Shaders::shortHistoryVariance(1.0f, 1.25f, 2.0f), 0.25f * 4.0f / 3.0f);
            EXPECT_FLOAT_EQ(Shaders::shortHistoryVariance(1.0f, 1.25f, 3.0f), 0.25f);
            EXPECT_FLOAT_EQ(Shaders::shortHistoryVariance(1.0f, 1.25f, 4.0f), 0.25f);
            EXPECT_FLOAT_EQ(Shaders::shortHistoryVariance(1000.0f, 1.25e6f, 1.0f),
                1.0e6f * Shaders::shortHistoryVariance(1.0f, 1.25f, 1.0f));
            EXPECT_EQ(Shaders::shortHistoryVariance(2.0f, 4.0f, 1.0f), 0.0f);
            EXPECT_EQ(Shaders::momentVariance(2.0f, 3.9f), 0.0f);
        }
    }
}
