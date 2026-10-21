#include <cstdint>
#include <set>
#include <utility>

#include <gtest/gtest.h>

#include <components/rtxvulkan/shaders/shared/fields.h>

namespace Rtx
{
    namespace
    {
        using Shaders::fieldColumns;
        using Shaders::fieldGroupsAcross;
        using Shaders::fieldOf;
        using Shaders::fieldSwapOf;
        using Shaders::pixelOf;
        using Shaders::storedInField;
        using Shaders::storedOf;
        using Shaders::storedWidth;
        using Shaders::uvec2;

        /// **A row of five, by hand.** Each field's half holds three columns. With no swap the first
        /// field is the even pixels of an even row — 0, 2 and 4, stored at 0, 1 and 2 — and the
        /// second the odd ones, 1 and 3, stored at 3 and 4; the slot at 5 holds no pixel, and reads
        /// back as the column past the frame. On the next row the parities change places, and the
        /// swap changes them back.
        TEST(RtxFieldsTest, aRowOfFiveIsStoredAsTwoHalvesOfThreeByHand)
        {
            EXPECT_EQ(fieldColumns(5), 3u);
            EXPECT_EQ(fieldColumns(4), 2u);
            EXPECT_EQ(storedWidth(5), 6u) << "an odd row keeps one slot empty";
            EXPECT_EQ(storedWidth(4), 4u);

            EXPECT_EQ(storedOf(uvec2(0, 0), 0, 5), uvec2(0, 0));
            EXPECT_EQ(storedOf(uvec2(2, 0), 0, 5), uvec2(1, 0));
            EXPECT_EQ(storedOf(uvec2(4, 0), 0, 5), uvec2(2, 0));
            EXPECT_EQ(storedOf(uvec2(1, 0), 0, 5), uvec2(3, 0));
            EXPECT_EQ(storedOf(uvec2(3, 0), 0, 5), uvec2(4, 0));
            EXPECT_EQ(pixelOf(uvec2(5, 0), 0, 5), uvec2(5, 0)) << "the empty slot reads past the frame";

            EXPECT_EQ(storedOf(uvec2(1, 1), 0, 5), uvec2(0, 1)) << "an odd row's odd pixel is the first field";
            EXPECT_EQ(storedOf(uvec2(0, 1), 0, 5), uvec2(3, 1));
            EXPECT_EQ(storedOf(uvec2(0, 0), 1, 5), uvec2(3, 0)) << "the swap turns the fields over";
            EXPECT_EQ(fieldOf(uvec2(0, 0), 1), 1u);

            // A place in the other field's half is the neighbour along the row: pixel 2 of the first
            // field at slot 1 of its own half, and at slot 4 of the second's, where pixel 3 is.
            EXPECT_EQ(storedInField(uvec2(2, 0), 0, 5), uvec2(1, 0));
            EXPECT_EQ(storedInField(uvec2(2, 0), 1, 5), uvec2(4, 0));
            EXPECT_EQ(pixelOf(uvec2(4, 0), 0, 5), uvec2(3, 0));

            // The swap is the frame number's parity: two frames running lay the fields both ways,
            // and frame 7 as frame 1 does.
            EXPECT_EQ(fieldSwapOf(6), 0u);
            EXPECT_EQ(fieldSwapOf(7), 1u);
            EXPECT_NE(fieldSwapOf(6), fieldSwapOf(7));

            // A field of three columns in groups of two takes two groups, and of four takes two:
            // (3 + 1) / 2 and (4 + 1) / 2, the frames five and seven wide.
            EXPECT_EQ(fieldGroupsAcross(5, 2), 2u);
            EXPECT_EQ(fieldGroupsAcross(7, 2), 2u);
            EXPECT_EQ(fieldGroupsAcross(9, 2), 3u) << "five columns";
            EXPECT_EQ(fieldGroupsAcross(16, 8), 1u);
            EXPECT_EQ(fieldGroupsAcross(18, 8), 2u);
        }

        /// **Every pixel is stored once and read back as itself**, at every width to 9, both swaps,
        /// over three rows; the stored slots a frame fills are every slot of its rows but the one an
        /// odd width leaves, and each pixel is in the half its field names, where `storedInField` of
        /// its own field puts it.
        TEST(RtxFieldsTest, everyPixelIsStoredOnceInItsFieldsHalfAndReadBackAsItself)
        {
            for (std::uint32_t width = 1; width <= 9; ++width)
                for (std::uint32_t swap = 0; swap < 2; ++swap)
                {
                    std::set<std::pair<std::uint32_t, std::uint32_t>> filled;
                    for (std::uint32_t y = 0; y < 3; ++y)
                        for (std::uint32_t x = 0; x < width; ++x)
                        {
                            const uvec2 pixel(x, y);
                            const uvec2 stored = storedOf(pixel, swap, width);
                            EXPECT_EQ(pixelOf(stored, swap, width), pixel) << width << " wide, swap " << swap;
                            EXPECT_EQ(storedInField(pixel, fieldOf(pixel, swap), width), stored);
                            EXPECT_LT(stored[0], storedWidth(width));
                            EXPECT_EQ(stored[0] >= fieldColumns(width), fieldOf(pixel, swap) == 1)
                                << "pixel " << x << ", " << y << " in the other field's half";
                            EXPECT_TRUE(filled.emplace(stored[0], stored[1]).second)
                                << "two pixels stored at " << stored[0] << ", " << stored[1];
                        }

                    const std::uint32_t empty = (width % 2) * 3;
                    EXPECT_EQ(filled.size() + empty, storedWidth(width) * 3) << width << " wide";
                }
        }
    }
}
