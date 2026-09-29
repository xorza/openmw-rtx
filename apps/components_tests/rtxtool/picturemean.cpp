#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/picturemean.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A byte's mean, rounded to the nearest and a half up.** One pixel's four bytes across
        /// three pictures, by hand: 0, 1, 1 sum to 2, two thirds, and round to 1; 0, 0, 1 to a third
        /// and round to 0; 255 thrice stays 255; and 7, 8 and 9 are 8. Two pictures of 0 and 1 are a
        /// half, and round up.
        TEST(RtxPictureMeanTest, aByteIsTheMeanOfItsPicturesRoundedToTheNearest)
        {
            PictureMean mean;
            const std::array<std::array<std::uint8_t, 4>, 3> pictures{ {
                { 0, 0, 255, 7 },
                { 1, 0, 255, 8 },
                { 1, 1, 255, 9 },
            } };
            for (const std::array<std::uint8_t, 4>& picture : pictures)
                mean.add(picture, 1, 1);

            EXPECT_EQ(mean.getCount(), 3u);
            std::vector<std::uint8_t> pixels;
            mean.mean(pixels);
            EXPECT_EQ(pixels, (std::vector<std::uint8_t>{ 1, 0, 255, 8 }));

            mean.clear();
            EXPECT_EQ(mean.getCount(), 0u);
            const std::array<std::uint8_t, 8> none{ 0, 0, 0, 0, 0, 0, 0, 0 };
            const std::array<std::uint8_t, 8> one{ 1, 1, 1, 1, 1, 1, 1, 1 };
            mean.add(none, 2, 1);
            mean.add(one, 2, 1);
            EXPECT_EQ(mean.getWidth(), 2u);
            mean.mean(pixels);
            EXPECT_EQ(pixels, (std::vector<std::uint8_t>(8, 1))) << "a half rounds up";
        }
    }
}
