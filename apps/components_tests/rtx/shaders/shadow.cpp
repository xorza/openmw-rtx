#include <gtest/gtest.h>

#include <components/rtx/shaders/shadow.h>

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
    }
}
