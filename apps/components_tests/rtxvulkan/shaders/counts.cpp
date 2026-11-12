#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/counts.h>

namespace Rtx::Shaders
{
    namespace
    {
        /// **The census keys the topmost, then the leftmost, invocation largest**, so the largest written is
        /// the first in reading order, and no invocation keys nought, which a cleared census holds for none: a
        /// row up beats any column, a column left beats the next, and the far corner, past what a frame
        /// reaches, stops a row short of nought.
        TEST(RtxCensusFirstTest, theTopmostThenLeftmostInvocationKeysLargestAndNoneKeysNought)
        {
            EXPECT_EQ(censusFirst(0, 0), 0xffffffffu);
            EXPECT_EQ(censusFirst(3, 5), 0xffffffffu - ((5u << 16u) | 3u));
            EXPECT_GT(censusFirst(65535, 4), censusFirst(0, 5)) << "a row up beats any column";
            EXPECT_GT(censusFirst(2, 9), censusFirst(3, 9)) << "a column left beats the next";
            EXPECT_EQ(censusFirst(65535, 65535), 0xffffffffu - ((0xfffeu << 16u) | 0xffffu));
            EXPECT_NE(censusFirst(65535, 65535), 0u);
        }

        /// **The host reads back the x and y the census keyed**, each from its own half, at both ends of what a
        /// half holds.
        TEST(RtxCensusFirstTest, theHostReadsBackTheInvocationTheCensusKeyed)
        {
            EXPECT_EQ(censusFirstX(censusFirst(812, 403)), 812u);
            EXPECT_EQ(censusFirstY(censusFirst(812, 403)), 403u);
            EXPECT_EQ(censusFirstX(censusFirst(0, 0)), 0u);
            EXPECT_EQ(censusFirstY(censusFirst(0, 0)), 0u);
            EXPECT_EQ(censusFirstX(censusFirst(65535, 65534)), 65535u);
            EXPECT_EQ(censusFirstY(censusFirst(65535, 65534)), 65534u);
        }
    }
}
