#include <bit>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include <components/rtx/common/halffloat.hpp>

namespace Rtx
{
    namespace
    {
        /// **Every half read back is the half it was**: the two conversions are each other's
        /// inverse over all 63 488 halves that are numbers, both zeros, both infinities and every
        /// subnormal included.
        TEST(RtxHalfFloatTest, everyHalfComesBackAsItself)
        {
            for (std::uint32_t bits = 0; bits <= 0xffffu; ++bits)
            {
                const auto half = static_cast<std::uint16_t>(bits);
                const bool nan = (half & 0x7c00u) == 0x7c00u && (half & 0x3ffu) != 0;
                if (nan)
                    continue;
                ASSERT_EQ(toHalf(fromHalf(half)), half) << "half " << bits;
            }
        }

        /// **The nearest half, ties to the even one**, at the edges a hand can name: one is
        /// `3c00`; one and half a step (2^-11) is a tie that the even `3c00` takes, and one and one
        /// and a half steps rounds up to the even `3c02`; the largest half is `7bff`, 65520 half way
        /// past it ties to the infinity, and just under 65520 stays `7bff`; the smallest normal
        /// `0400`, the smallest subnormal `0001`, half of it a tie to nought and a little more to
        /// `0001`; a sign on each; and an infinity and a NaN as themselves.
        TEST(RtxHalfFloatTest, aFloatRoundsToTheNearestHalfAndATieToTheEvenOne)
        {
            EXPECT_EQ(toHalf(1.0f), 0x3c00u);
            EXPECT_EQ(toHalf(1.0f + 0x1p-11f), 0x3c00u);
            EXPECT_EQ(toHalf(1.0f + 0x1.8p-10f), 0x3c02u);
            EXPECT_EQ(toHalf(1.0f + 0x1p-10f), 0x3c01u);
            EXPECT_EQ(toHalf(65504.0f), 0x7bffu);
            EXPECT_EQ(toHalf(65520.0f), 0x7c00u);
            EXPECT_EQ(toHalf(65519.0f), 0x7bffu);
            EXPECT_EQ(toHalf(0x1p-14f), 0x0400u);
            EXPECT_EQ(toHalf(0x1p-24f), 0x0001u);
            EXPECT_EQ(toHalf(0x1p-25f), 0x0000u);
            EXPECT_EQ(toHalf(0x1.8p-25f), 0x0001u);
            EXPECT_EQ(toHalf(0x1.8p-24f), 0x0002u) << "one and a half steps is a tie the even two takes";
            EXPECT_EQ(toHalf(-2.0f), 0xc000u);
            EXPECT_EQ(toHalf(-0.0f), 0x8000u);
            EXPECT_EQ(toHalf(std::numeric_limits<float>::infinity()), 0x7c00u);
            EXPECT_EQ(toHalf(-std::numeric_limits<float>::infinity()), 0xfc00u);
            EXPECT_EQ(toHalf(std::numeric_limits<float>::max()), 0x7c00u);
            const std::uint16_t nan = toHalf(std::numeric_limits<float>::quiet_NaN());
            EXPECT_EQ(nan & 0x7c00u, 0x7c00u);
            EXPECT_NE(nan & 0x3ffu, 0u);
        }
    }
}
