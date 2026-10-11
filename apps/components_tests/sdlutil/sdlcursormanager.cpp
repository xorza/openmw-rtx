#include <components/sdlutil/sdlcursormanager.hpp>

#include <gtest/gtest.h>

namespace SDLUtil
{
    namespace
    {
        /// **A cursor's image is the size the driver scales back up, and X11 scales nothing.** At a
        /// content scale of 1.5 an X11 cursor's image is its whole size, divided by one; Wayland,
        /// Windows and macOS show the image at the window's points, so theirs is divided by the
        /// display's scale of 1.5, and the whole size goes beside it as the alternate SDL picks.
        TEST(SDLUtilCursorManagerTest, aCursorsImageIsDividedByWhatTheDriverScalesItBy)
        {
            EXPECT_EQ(SDLCursorManager::scalingOf("x11"), CursorScaling::AsPixels);
            for (const char* scaled : { "wayland", "windows", "cocoa", "" })
                EXPECT_EQ(SDLCursorManager::scalingOf(scaled), CursorScaling::ByDisplay) << scaled;

            EXPECT_EQ(SDLCursorManager::baseDivisor(CursorScaling::AsPixels, 1.5f), 1.f);
            EXPECT_EQ(SDLCursorManager::baseDivisor(CursorScaling::ByDisplay, 1.5f), 1.5f);
            EXPECT_EQ(SDLCursorManager::baseDivisor(CursorScaling::ByDisplay, 1.f), 1.f);
        }
    }
}
