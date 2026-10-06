#include <array>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <apps/rtxtool/picturemean.hpp>

namespace RtxTool
{
    namespace
    {
        /// **A byte's mean on the sixteen-bit scale, rounded to the nearest and a half up**, a level
        /// being 257. One pixel's four bytes across three pictures, by hand: 0, 1, 1 sum to 2, so
        /// `2 * 257 / 3` = 171.33, 171; 0, 0, 1 to `257 / 3` = 85.67, 86; 255 thrice is 65535; and
        /// 7, 8 and 9 are `8 * 257` = 2056. Two pictures of 0 and 1 are `257 / 2` = 128.5, and round
        /// up to 129: a mean a byte would round to 1, kept here at half a level.
        TEST(RtxPictureMeanTest, aMeanIsKeptBetweenTheBytesRoundedToTheNearest)
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
            std::vector<std::uint16_t> samples;
            mean.mean(samples);
            EXPECT_EQ(samples, (std::vector<std::uint16_t>{ 171, 86, 65535, 2056 }));

            mean.clear();
            EXPECT_EQ(mean.getCount(), 0u);
            const std::array<std::uint8_t, 8> none{ 0, 0, 0, 0, 0, 0, 0, 0 };
            const std::array<std::uint8_t, 8> one{ 1, 1, 1, 1, 1, 1, 1, 1 };
            mean.add(none, 2, 1);
            mean.add(one, 2, 1);
            EXPECT_EQ(mean.getWidth(), 2u);
            mean.mean(samples);
            EXPECT_EQ(samples, (std::vector<std::uint16_t>(8, 129))) << "a half rounds up";
        }
    }
}
