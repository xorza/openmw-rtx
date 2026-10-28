#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/common/radicalinverse.hpp>

namespace Rtx
{
    namespace
    {
        /// **The digits reflected about the point, and the quotient rounded once.** Six is `110` in
        /// base two, so `0.011` is 3/8, exact. Five is `12` in base three, so `0.21` is 7/9; ten is
        /// `101`, its own reflection, 10/27; and the jitter's 1000th term, `1101001` in base three, is
        /// `0.1001011` = 760/2187. Each against the double's quotient rounded to a float, which is
        /// the float nearest the fraction. Nought is nought in every base. Base three's tenth term
        /// and base five's 24th, `44` and so 24/25, are where a place divided by the base at each
        /// digit rounded more than once and landed an ulp off.
        TEST(RtxRadicalInverseTest, theTermIsItsDigitsReflectedAndRoundedOnce)
        {
            EXPECT_EQ(radicalInverse(0, 2), 0.0f);
            EXPECT_EQ(radicalInverse(0, 3), 0.0f);
            EXPECT_EQ(radicalInverse(6, 2), 0.375f);
            EXPECT_EQ(radicalInverse(5, 3), static_cast<float>(7.0 / 9.0));
            EXPECT_EQ(radicalInverse(10, 3), static_cast<float>(10.0 / 27.0));
            EXPECT_EQ(radicalInverse(1000, 3), static_cast<float>(760.0 / 2187.0));
            EXPECT_EQ(radicalInverse(24, 5), static_cast<float>(24.0 / 25.0));

            // The largest index in base two is every digit reflected: `0.111…1`, 32 ones, which is
            // one less 2⁻³² and rounds to one in a float.
            EXPECT_EQ(radicalInverse(UINT32_MAX, 2), 1.0f);
        }
    }
}
