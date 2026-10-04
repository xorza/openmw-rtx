#include <cmath>

#include <gtest/gtest.h>

#include <components/rtx/shaders/bouncereuse.h>

namespace Rtx
{
    namespace
    {
        /// **The Jacobian of a reconnection, by hand.** A sample at the origin facing up, found from
        /// a point a hundred units above it, `found = 100² = 10000` and facing it squarely: its
        /// cosine is `100 / 100 = 1`.
        ///
        /// - Shifted to the point that found it, nothing changes: one.
        /// - Shifted to `(100, 0, 100)`: `seen = 20000`, the cosine `100 / √20000 = 1/√2`, and the
        ///   Jacobian `(1/√2) 10000 / (1 · 20000) = √2 / 4`.
        /// - Shifted to `(0, 0, -100)`, the far side of the sample's surface: refused.
        /// - Shifted to a point the sample meets edge-on, `(100, 0, 0)`: refused.
        TEST(RtxBounceReuseShiftTest, theJacobianOfAReconnectionIsTheRatioOfItsSolidAngles)
        {
            EXPECT_EQ(Shaders::reconnectionJacobian(100.0f, 10000.0f, 100.0f, 10000.0f), 1.0f);
            EXPECT_FLOAT_EQ(Shaders::reconnectionJacobian(100.0f, 10000.0f, 100.0f, 20000.0f), std::sqrt(2.0f) / 4.0f);
            EXPECT_EQ(Shaders::reconnectionJacobian(100.0f, 10000.0f, -100.0f, 10000.0f), 0.0f);
            EXPECT_EQ(Shaders::reconnectionJacobian(100.0f, 10000.0f, 0.0f, 10000.0f), 0.0f);
        }

        /// **The limit holds both ways, and takes its own bound.** Found ten units straight above and
        /// shifted straight up to `√1000` units, `found = 100` and `seen = 1000`, both cosines one: the
        /// Jacobian is `100 / 1000`, a tenth, which `BOUNCE_JACOBIAN_LIMIT` lets through; a step
        /// further is past it, and so is the shift the other way, ten.
        TEST(RtxBounceReuseShiftTest, aReconnectionPastTheLimitIsRefused)
        {
            const float far = std::sqrt(1000.0f);
            EXPECT_EQ(
                Shaders::reconnectionJacobian(10.0f, 100.0f, far, 1000.0f), 1.0f / Shaders::BOUNCE_JACOBIAN_LIMIT);

            const float further = std::sqrt(1001.0f);
            EXPECT_EQ(Shaders::reconnectionJacobian(10.0f, 100.0f, further, 1001.0f), 0.0f);
            EXPECT_EQ(Shaders::reconnectionJacobian(far, 1000.0f, 10.0f, 100.0f), Shaders::BOUNCE_JACOBIAN_LIMIT);
            EXPECT_EQ(Shaders::reconnectionJacobian(further, 1001.0f, 10.0f, 100.0f), 0.0f);
        }

        /// **Two visible points are alike the same way round from either**, which the pairs need: a
        /// pixel reads its partner's bit only where it takes the partner, and the partner traced it
        /// only where it took the pixel. At `BOUNCE_DEPTH`, a tenth of the nearer distance: 100 and 110
        /// are alike (10 ≤ 10), 100 and 111 are not (11 > 10), and 90 and 100 are not (10 > 9), from
        /// either end. RTXDI's tenth of the centre's distance would take 90 from 100 (10 ≤ 10) and
        /// refuse 100 from 90. Facing at `BOUNCE_FACING` = 0.6 is alike, and below it is not.
        TEST(RtxBounceReuseAlikeTest, twoPointsAreAlikeTheSameWayRound)
        {
            EXPECT_TRUE(Shaders::bounceAlike(0.6f, 100.0f, 110.0f));
            EXPECT_FALSE(Shaders::bounceAlike(0.6f, 100.0f, 111.0f));
            EXPECT_FALSE(Shaders::bounceAlike(0.59f, 100.0f, 100.0f));
            EXPECT_TRUE(Shaders::bounceAlike(1.0f, 90.0f, 99.0f));
            EXPECT_FALSE(Shaders::bounceAlike(1.0f, 90.0f, 100.0f));

            for (const float first : { 1.0f, 50.0f, 90.0f, 100.0f, 2000.0f })
                for (const float second : { 1.05f, 55.0f, 99.0f, 100.0f, 110.0f, 111.0f, 2150.0f })
                    for (const float facing : { 0.59f, 0.6f, 0.9f })
                        EXPECT_EQ(
                            Shaders::bounceAlike(facing, first, second), Shaders::bounceAlike(facing, second, first))
                            << first << " and " << second;
        }
    }
}
