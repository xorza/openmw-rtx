#include <array>
#include <cstddef>
#include <optional>

#include <gtest/gtest.h>

#include <components/rtxvulkan/device/commands.hpp>

namespace Rtx
{
    namespace
    {
        constexpr VkDeviceSize sMiB = 1024 * 1024;

        /// **The smallest spare block that holds an upload, by hand.** Blocks 0 to 3 of 8, 36, 9 and
        /// 8 MiB, spare in the order 1, 0, 2, 3, which is not the order of their sizes or their
        /// indices:
        ///
        /// - a hundred bytes takes an 8 MiB block, and of the two the lower, block 0, at position 1,
        ///   where first fit took the 36 MiB block standing first;
        /// - 8 MiB and a byte takes the 9 MiB block, block 2, at position 2;
        /// - 36 MiB exactly takes block 1, which holds it to the byte, at position 0;
        /// - 40 MiB takes nothing, and nor does an empty spare list.
        TEST(RtxStagingFitTest, anUploadTakesTheSmallestSpareBlockThatHoldsIt)
        {
            constexpr std::array<VkDeviceSize, 4> sizes{ 8 * sMiB, 36 * sMiB, 9 * sMiB, 8 * sMiB };
            constexpr std::array<std::size_t, 4> spare{ 1, 0, 2, 3 };
            const auto sizeOf = [&](const std::size_t block) { return sizes[block]; };

            EXPECT_EQ(CommandPool::bestFit(spare, sizeOf, 100), std::optional<std::size_t>(1));
            EXPECT_EQ(CommandPool::bestFit(spare, sizeOf, 8 * sMiB + 1), std::optional<std::size_t>(2));
            EXPECT_EQ(CommandPool::bestFit(spare, sizeOf, 36 * sMiB), std::optional<std::size_t>(0));
            EXPECT_EQ(CommandPool::bestFit(spare, sizeOf, 40 * sMiB), std::nullopt);
            EXPECT_EQ(CommandPool::bestFit({}, sizeOf, 100), std::nullopt);

            constexpr std::array<std::size_t, 2> tiedBackwards{ 3, 0 };
            EXPECT_EQ(CommandPool::bestFit(tiedBackwards, sizeOf, 100), std::optional<std::size_t>(1))
                << "a tie went to the block given back first and not the lower one";
        }
    }
}
