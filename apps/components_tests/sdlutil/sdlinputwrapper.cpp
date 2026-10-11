#include <components/sdlutil/sdlinputwrapper.hpp>

#include <vector>

#include <gtest/gtest.h>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_video.h>

#include <components/sdlutil/events.hpp>
#include <components/sdlutil/graphicslistener.hpp>

namespace SDLUtil
{
    namespace
    {
        struct NoGraphics final : GraphicsListener
        {
        };

        /// What the window listener was told, in order.
        struct Visibility final : WindowListener
        {
            void windowVisibilityChange(bool visible) override { mTold.push_back(visible); }

            std::vector<bool> mTold;
        };

        /// SDL's video on its offscreen driver, which needs no display.
        struct OffscreenVideo
        {
            OffscreenVideo()
            {
                SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
                mStarted = SDL_InitSubSystem(SDL_INIT_VIDEO);
            }

            ~OffscreenVideo()
            {
                if (mWindow != nullptr)
                    SDL_DestroyWindow(mWindow);
                if (mStarted)
                    SDL_QuitSubSystem(SDL_INIT_VIDEO);
            }

            OffscreenVideo(const OffscreenVideo&) = delete;
            OffscreenVideo& operator=(const OffscreenVideo&) = delete;

            bool mStarted = false;
            SDL_Window* mWindow = nullptr;
        };

        /// **The listener hears the window's state, not its events**: once when the window hides,
        /// nothing for an event that leaves the state as it was, and once when it shows again. An
        /// exposure while hidden is not a window shown, and an occlusion SDL never flagged is not
        /// one hidden: each is pushed by hand, as a system that sends edges out of order would.
        TEST(SDLUtilInputWrapperTest, theListenerHearsTheWindowsStateOncePerChange)
        {
            OffscreenVideo video;
            ASSERT_TRUE(video.mStarted) << SDL_GetError();
            video.mWindow = SDL_CreateWindow("input", 64, 32, 0);
            ASSERT_NE(video.mWindow, nullptr) << SDL_GetError();

            NoGraphics graphics;
            Visibility listener;
            InputWrapper input(video.mWindow, graphics, false);
            input.setWindowEventCallback(&listener);

            const auto push = [&](SDL_EventType type) {
                SDL_Event event{};
                event.window.type = type;
                event.window.windowID = SDL_GetWindowID(video.mWindow);
                ASSERT_TRUE(SDL_PushEvent(&event)) << SDL_GetError();
            };

            push(SDL_EVENT_WINDOW_OCCLUDED);
            input.capture(true);
            EXPECT_EQ(listener.mTold, std::vector<bool>{}) << "an occlusion the flags do not hold";

            ASSERT_TRUE(SDL_HideWindow(video.mWindow)) << SDL_GetError();
            input.capture(true);
            EXPECT_EQ(listener.mTold, std::vector<bool>{ false });

            push(SDL_EVENT_WINDOW_EXPOSED);
            push(SDL_EVENT_WINDOW_RESTORED);
            input.capture(true);
            EXPECT_EQ(listener.mTold, std::vector<bool>{ false }) << "a hidden window told it shows";

            ASSERT_TRUE(SDL_ShowWindow(video.mWindow)) << SDL_GetError();
            input.capture(true);
            EXPECT_EQ(listener.mTold, (std::vector<bool>{ false, true }));
        }
    }
}
