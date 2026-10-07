#include <components/sdlutil/sdlvideowrapper.hpp>

#include <gtest/gtest.h>

namespace SDLUtil
{
    namespace
    {
        /// **A window is sized so that its pixels are the resolution asked for, at any density.** 3840
        /// by 2160 pixels at one and a half pixels a point is 3840 / 1.5 = 2560 by 2160 / 1.5 = 1440
        /// points; upstream's `3840 / (5760 / 3840)` divided in whole numbers, came to 3840 again,
        /// and left the window at one and a half times the resolution. At two it is 1920 by 1080, at
        /// one the pixels themselves, and at 1.25 a size that does not divide whole is the nearest
        /// point: 1366 / 1.25 = 1092.8, so 1093.
        TEST(SDLUtilVideoWrapperTest, aWindowsPointsGiveThePixelsAskedForAtAnyDensity)
        {
            EXPECT_EQ(windowPoints(3840, 1.5f), 2560);
            EXPECT_EQ(windowPoints(2160, 1.5f), 1440);
            EXPECT_EQ(windowPoints(3840, 2.f), 1920);
            EXPECT_EQ(windowPoints(2160, 2.f), 1080);
            EXPECT_EQ(windowPoints(3840, 1.f), 3840);
            EXPECT_EQ(windowPoints(1366, 1.25f), 1093);

            // **And back to pixels to the nearest**: 3840 pixels on 2567 points is a density of
            // 1.4959096 in a float, and 2567 times it is 3839.9998, which truncated came to 3839 and
            // listed a mode a pixel narrower than the display's.
            const float fractional = 3840.0f / 2567.0f;
            EXPECT_LT(static_cast<int>(2567.0f * fractional), 3840) << "the float falls short, or this proves nothing";
            EXPECT_EQ(displayPixels(2567, fractional), 3840);
            EXPECT_EQ(displayPixels(2560, 1.5f), 3840);
            EXPECT_EQ(displayPixels(1093, 1.25f), 1366);
        }
    }
}
