#include <gtest/gtest.h>

#include <components/rtxvulkan/device/memory/largestowed.hpp>

namespace Rtx
{
    namespace
    {
        /// **The largest figure any owner owes, and not their sum**: owners of 64, 128 and 32 leave 128; the 128 owing
        /// 16 instead leaves 64; two owners of one figure stand apart, so one taking its figure back leaves the
        /// other's; and every owner owing nothing leaves nought.
        TEST(RtxLargestOwedTest, isTheLargestOneOwnerOwesWhateverTheOthersOwe)
        {
            LargestOwed owed;
            EXPECT_EQ(owed.largest(), 0u) << "no owner";

            owed.change(0, 64);
            owed.change(0, 128);
            owed.change(0, 32);
            EXPECT_EQ(owed.largest(), 128u) << "the largest of 64, 128 and 32, where their sum is 224";

            owed.change(128, 16);
            EXPECT_EQ(owed.largest(), 64u) << "the 128 owing 16";

            owed.change(0, 64);
            owed.change(64, 0);
            EXPECT_EQ(owed.largest(), 64u) << "a second owner of 64 still owes it";

            owed.change(64, 0);
            EXPECT_EQ(owed.largest(), 32u);

            owed.change(32, 32);
            EXPECT_EQ(owed.largest(), 32u) << "a figure owed again as it was";

            owed.change(32, 0);
            owed.change(16, 0);
            EXPECT_EQ(owed.largest(), 0u) << "every owner owing nothing";
        }
    }
}
