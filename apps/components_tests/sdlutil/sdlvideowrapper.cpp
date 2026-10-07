#include <components/sdlutil/sdlvideowrapper.hpp>

#include <gtest/gtest.h>

namespace
{
    TEST(SDLUtilWindowPointsTest, shouldGiveRequestedPixelsAtAnyDensity)
    {
        EXPECT_EQ(SDLUtil::windowPoints(3840, 1.5f), 2560);
        EXPECT_EQ(SDLUtil::windowPoints(2160, 1.5f), 1440);
        EXPECT_EQ(SDLUtil::windowPoints(3840, 2.f), 1920);
        EXPECT_EQ(SDLUtil::windowPoints(2160, 2.f), 1080);
        EXPECT_EQ(SDLUtil::windowPoints(3840, 1.f), 3840);
        EXPECT_EQ(SDLUtil::windowPoints(1366, 1.25f), 1093);
    }

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
