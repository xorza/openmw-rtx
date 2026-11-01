#include <optional>

#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <apps/openmw/mwrender/rtx/rtxwindow.hpp>

namespace MWRender
{
    namespace
    {
        /// SDL's video on its offscreen driver, which needs no display, and the driver hint put back.
        struct OffscreenVideo
        {
            OffscreenVideo()
            {
                SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
                mStarted = SDL_InitSubSystem(SDL_INIT_VIDEO);
            }

            ~OffscreenVideo()
            {
                if (mStarted)
                    SDL_QuitSubSystem(SDL_INIT_VIDEO);
                SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
            }

            OffscreenVideo(const OffscreenVideo&) = delete;
            OffscreenVideo& operator=(const OffscreenVideo&) = delete;

            bool mStarted = false;
        };

        /// **A window on SDL's offscreen driver has no display, and is made with no surface**: what a
        /// run on a box with no monitor, and the gate's integration step, make the window on, which
        /// the backend is then handed none of. A window made with a backend's flags has a surface
        /// whatever they are, as the null backend's are none.
        TEST(RtxWindowTest, aWindowOnTheOffscreenDriverHasNoDisplayAndIsMadeWithNoSurface)
        {
            const OffscreenVideo video;
            ASSERT_TRUE(video.mStarted) << SDL_GetError();
            EXPECT_FALSE(RtxWindow::hasDisplay());

            const RtxWindow nowhere(std::nullopt, true, std::nullopt);
            EXPECT_FALSE(nowhere.hasSurface());
            EXPECT_EQ(SDL_GetWindowFlags(nowhere.get()) & SDL_WINDOW_VULKAN, 0u);

            const RtxWindow given(SDL_WindowFlags{ 0 }, true, std::nullopt);
            EXPECT_TRUE(given.hasSurface());
        }
    }
}
