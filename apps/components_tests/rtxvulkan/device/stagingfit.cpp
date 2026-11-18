#include <array>
#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/memory/stagingring.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkDeviceSize sMiB = 1024 * 1024;

        /// **The smallest spare block that holds an upload, by hand.** Blocks 0 to 3 of 8, 36, 9 and 8 MiB, spare in
        /// the order 1, 0, 2, 3, which is not the order of their sizes or indices:
        ///
        /// - a hundred bytes takes an 8 MiB block, and of the two the lower, block 0, at position 1, where first fit
        ///   took the 36 MiB block standing first;
        /// - 8 MiB and a byte takes the 9 MiB block, block 2, at position 2;
        /// - 36 MiB exactly takes block 1, at position 0;
        /// - 40 MiB takes nothing, and nor does an empty spare list.
        TEST(RtxStagingFitTest, anUploadTakesTheSmallestSpareBlockThatHoldsIt)
        {
            constexpr std::array<VkDeviceSize, 4> sizes{ 8 * sMiB, 36 * sMiB, 9 * sMiB, 8 * sMiB };
            constexpr std::array<std::size_t, 4> spare{ 1, 0, 2, 3 };
            const auto sizeOf = [&](const std::size_t block) { return sizes[block]; };

            EXPECT_EQ(StagingRing::bestFit(spare, sizeOf, 100), std::optional<std::size_t>(1));
            EXPECT_EQ(StagingRing::bestFit(spare, sizeOf, 8 * sMiB + 1), std::optional<std::size_t>(2));
            EXPECT_EQ(StagingRing::bestFit(spare, sizeOf, 36 * sMiB), std::optional<std::size_t>(0));
            EXPECT_EQ(StagingRing::bestFit(spare, sizeOf, 40 * sMiB), std::nullopt);
            EXPECT_EQ(StagingRing::bestFit({}, sizeOf, 100), std::nullopt);

            constexpr std::array<std::size_t, 2> tiedBackwards{ 3, 0 };
            EXPECT_EQ(StagingRing::bestFit(tiedBackwards, sizeOf, 100), std::optional<std::size_t>(1))
                << "a tie went to the block given back first and not the lower one";
        }

        /// **A spare block is shed only once it has stood unused for `sStagingIdleSubmits`, the largest first.** Blocks
        /// 0 to 2 of 8, 36 and 9 MiB, given back at submits 100, 50 and 700. By submit 649 none has cooled; by 650,
        /// block 1, given back at 50, is the one; by 1300 all three have, and the largest, block 1, goes first.
        TEST(RtxStagingFitTest, aSpareBlockIsShedOnlyOnceItHasCooled)
        {
            const std::array sizes{ 8 * sMiB, 36 * sMiB, 9 * sMiB };
            const std::array<std::uint64_t, 3> freedAt{ 100, 50, 700 };
            const std::array<std::size_t, 3> spare{ 0, 1, 2 };
            const auto sizeOf = [&](const std::size_t block) { return sizes[block]; };
            const auto freed = [&](const std::size_t block) { return freedAt[block]; };

            EXPECT_EQ(StagingRing::sheddable(spare, sizeOf, freed, 649), std::nullopt);
            EXPECT_EQ(StagingRing::sheddable(spare, sizeOf, freed, 650), std::optional<std::size_t>(1));
            EXPECT_EQ(StagingRing::sheddable(spare, sizeOf, freed, 1300), std::optional<std::size_t>(1));

            const std::array<std::uint64_t, 3> laterLargest{ 100, 900, 120 };
            const auto freedLater = [&](const std::size_t block) { return laterLargest[block]; };
            EXPECT_EQ(StagingRing::sheddable(spare, sizeOf, freedLater, 720), std::optional<std::size_t>(2))
                << "of those cooled, 8 and 9 MiB, the larger";
        }
    }
}
