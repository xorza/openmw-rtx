#include <cmath>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/shadowalpha.hpp>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtxvulkan/shaders/shared/shadow.h>

namespace Rtx
{
    namespace
    {
        /// **Every count from one to the whole word, against the masks written out.** The full word is
        /// the case a left shift leaves undefined, and the classification's row reaches it where
        /// `SHADOW_WORKGROUP + 2 * SHADOW_REACH` is 32.
        TEST(RtxShadowTest, lowBitsSetsTheCountsLowBitsUpToTheWholeWord)
        {
            EXPECT_EQ(Shaders::lowBits(1), 0x1u);
            EXPECT_EQ(Shaders::lowBits(8), 0xFFu);
            EXPECT_EQ(Shaders::lowBits(24), 0xFFFFFFu);
            EXPECT_EQ(Shaders::lowBits(31), 0x7FFFFFFFu);
            EXPECT_EQ(Shaders::lowBits(32), 0xFFFFFFFFu);
        }

        /// **The penumbra's two marks against the reach they gate.** A drawn bit takes every level of
        /// the spatial filter, whose widest step is `1 << (SHADOW_FILTER_LEVELS - 1)`, four pixels,
        /// and passes the one pixel under which a bit counts as hard; a clear ray stands past both,
        /// and is a half, the channel's width, exactly.
        TEST(RtxShadowTest, theDrawnAndTheClearPenumbraStandPastEveryReach)
        {
            EXPECT_GT(Shaders::SHADOW_PENUMBRA_DRAWN, static_cast<float>(1u << (Shaders::SHADOW_FILTER_LEVELS - 1u)));
            EXPECT_GT(Shaders::SHADOW_PENUMBRA_DRAWN, 1.0f);
            EXPECT_GT(Shaders::SHADOW_PENUMBRA_CLEAR, Shaders::SHADOW_PENUMBRA_DRAWN);
            EXPECT_EQ(Shaders::SHADOW_PENUMBRA_CLEAR, 65504.0f) << "the largest finite half";
        }

        /// **The alpha carries the bit in its sign and the penumbra whole**, both marks and a radius
        /// of nought among them: closed at nought is `-0.0`, which reads closed, where `+0.0` would
        /// have read open.
        TEST(RtxShadowTest, aShadowAlphaCarriesTheBitInItsSignAndThePenumbraWhole)
        {
            for (const float penumbra :
                { 0.0f, 0.375f, Shaders::SHADOW_PENUMBRA_DRAWN, Shaders::SHADOW_PENUMBRA_CLEAR })
                for (const bool open : { false, true })
                {
                    const float alpha = Shaders::packShadowAlpha(open, penumbra);
                    EXPECT_EQ(Testing::shadowOpen(alpha), open) << penumbra;
                    EXPECT_EQ(Shaders::shadowPenumbra(alpha), penumbra) << penumbra << (open ? " open" : " closed");
                }
            EXPECT_TRUE(std::signbit(Shaders::packShadowAlpha(false, 0.0f))) << "a closed ray at nought";
        }
    }
}
