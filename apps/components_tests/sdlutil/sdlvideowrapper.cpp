#include <components/sdlutil/sdlvideowrapper.hpp>

#include <gtest/gtest.h>

namespace
{
    // 3840x2160 pixels at 1.5 pixels per point are 3840 / 1.5 = 2560 by 2160 / 1.5 = 1440 points. A ratio
    // in whole numbers, 5760 / 3840 = 1, would give a window of 3840x2160 points, 5760x3240 pixels. A size that
    // does not divide exactly is rounded to the nearest point: 1366 / 1.25 = 1092.8 gives 1093.
    TEST(SDLUtilWindowPointsTest, shouldGiveRequestedPixelsAtAnyDensity)
    {
        EXPECT_EQ(SDLUtil::windowPoints(3840, 1.5f), 2560);
        EXPECT_EQ(SDLUtil::windowPoints(2160, 1.5f), 1440);
        EXPECT_EQ(SDLUtil::windowPoints(3840, 2.f), 1920);
        EXPECT_EQ(SDLUtil::windowPoints(2160, 2.f), 1080);
        EXPECT_EQ(SDLUtil::windowPoints(3840, 1.f), 3840);
        EXPECT_EQ(SDLUtil::windowPoints(1366, 1.25f), 1093);
    }

    // A mode of 2560x1440 points at 1.5 pixels per point is 2560 * 1.5 = 3840 by 1440 * 1.5 = 2160 pixels. A size
    // that does not multiply exactly is rounded to the nearest pixel: 1093 * 1.25 = 1366.25 gives 1366, and
    // 615 * 1.25 = 768.75 gives 769.
    TEST(SDLUtilModePixelsTest, shouldGiveModeSizeInPixels)
    {
        SDL_DisplayMode mode{};
        mode.w = 2560;
        mode.h = 1440;
        mode.pixel_density = 1.5f;
        EXPECT_EQ(SDLUtil::modePixels(mode), osg::Vec2i(3840, 2160));
        mode.pixel_density = 1.f;
        EXPECT_EQ(SDLUtil::modePixels(mode), osg::Vec2i(2560, 1440));
        mode.w = 1093;
        mode.h = 615;
        mode.pixel_density = 1.25f;
        EXPECT_EQ(SDLUtil::modePixels(mode), osg::Vec2i(1366, 769));
    }
}
