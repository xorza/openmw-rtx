#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <components/rtxvulkan/device/retiring.hpp>

namespace Rtx
{
    namespace
    {
        /// **What a wait lets go of is the prefix its stamp reaches, in the order held**: entries at
        /// 1, 1, 2 and 5 let go of the first two at 1, nothing more at 1 again, and the rest at 5.
        TEST(RtxRetiringTest, aWaitLetsGoOfThePrefixItsStampReaches)
        {
            Retiring<int> held;
            held.hold(1, 10);
            held.hold(1, 11);
            held.hold(2, 12);
            held.hold(5, 13);

            std::vector<int> let;
            held.releaseThrough(1, [&](int each) { let.push_back(each); });
            EXPECT_EQ(let, (std::vector<int>{ 10, 11 }));
            EXPECT_EQ(held.size(), 2u);
            held.releaseThrough(1, [&](int each) { let.push_back(each); });
            EXPECT_EQ(held.size(), 2u) << "a wait at the same stamp let go of more";
            held.releaseThrough(5, [&](int each) { let.push_back(each); });
            EXPECT_EQ(let, (std::vector<int>{ 10, 11, 12, 13 }));
            EXPECT_EQ(held.size(), 0u);
        }

        /// **Order holds across the ring's wrap and its growth**: a hundred entries held three a stamp
        /// while a wait lets go of each stamp two behind, then all of them, come out as they went in.
        TEST(RtxRetiringTest, orderHoldsAcrossTheRingsWrapAndItsGrowth)
        {
            Retiring<int> held;
            std::vector<int> let;
            for (int each = 0; each < 100; ++each)
            {
                held.hold(static_cast<std::uint64_t>(each / 3), int{ each });
                if (each % 3 == 2 && each / 3 >= 2)
                    held.releaseThrough(static_cast<std::uint64_t>(each / 3 - 2), [&](int one) { let.push_back(one); });
            }
            held.releaseThrough(100, [&](int one) { let.push_back(one); });

            ASSERT_EQ(let.size(), 100u);
            for (int each = 0; each < 100; ++each)
                EXPECT_EQ(let[static_cast<std::size_t>(each)], each);
        }

        /// A stamp under one held before it would stand behind an entry a wait has not reached.
        TEST(RtxRetiringTest, aStampUnderOneHeldBeforeItDies)
        {
            Testing::expectAssertDies(
                [] {
                    Retiring<int> held;
                    held.hold(5, 1);
                    held.hold(4, 2);
                },
                "a stamp under one held before it");
        }
    }
}
