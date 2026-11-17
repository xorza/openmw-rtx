#include "sdlvideowrapper.hpp"

#include <cmath>
#include <cstdint>

#include <SDL3/SDL_video.h>

#include <components/debug/debuglog.hpp>
#include <components/settings/windowmode.hpp>

namespace SDLUtil
{
    namespace
    {
        void centerWindow(SDL_Window* window)
        {
            // Resize breaks the sdl window in some cases; see issue: #5539
            SDL_Rect rect{};
            std::int32_t w = 0;
            std::int32_t h = 0;
            SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &rect);

            // SDL3 sizes a window when the system gets round to it, and the size asked for a moment
            // ago is not yet the one this reads.
            SDL_SyncWindow(window);
            SDL_GetWindowSize(window, &w, &h);

            std::int32_t x = rect.x;
            std::int32_t y = rect.y;

            // Center dimensions that do not fill the screen
            if (w < rect.w)
                x = rect.x + rect.w / 2 - w / 2;
            if (h < rect.h)
                y = rect.y + rect.h / 2 - h / 2;

            SDL_SetWindowPosition(window, x, y);
        }
    }

    std::int32_t windowPoints(std::int32_t pixels, float density)
    {
        return static_cast<std::int32_t>(std::lround(static_cast<double>(pixels) / static_cast<double>(density)));
    }

    float pixelDensity(SDL_Window* window)
    {
        const float density = SDL_GetWindowPixelDensity(window);
        return density > 0.f ? density : 1.f;
    }

    float displayScale(SDL_Window* window)
    {
        const float scale = SDL_GetWindowDisplayScale(window);
        return scale > 0.f ? scale : 1.f;
    }

    std::int32_t displayPixels(std::int32_t points, float density)
    {
        return static_cast<std::int32_t>(std::lround(static_cast<double>(points) * static_cast<double>(density)));
    }

    bool isWindowShown(SDL_WindowFlags flags)
    {
        return (flags & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_OCCLUDED)) == 0;
    }

    bool isRestoredSize(SDL_WindowFlags flags)
    {
        return (flags & (SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED | SDL_WINDOW_FULLSCREEN)) == 0;
    }

    void setVideoMode(
        SDL_Window* window, std::int32_t width, std::int32_t height, Settings::WindowMode windowMode, bool windowBorder)
    {
        SDL_SetWindowFullscreen(window, false);

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)
            SDL_RestoreWindow(window);

        if (windowMode == Settings::WindowMode::Fullscreen)
        {
            // The listed mode nearest the desktop's, which is the desktop's own where the list has it:
            // SDL refuses a mode its list does not hold.
            const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
            SDL_DisplayMode mode;
            if (desktop == nullptr
                || !SDL_GetClosestFullscreenDisplayMode(
                    display, desktop->w, desktop->h, desktop->refresh_rate, true, &mode)
                || !SDL_SetWindowFullscreenMode(window, &mode))
                Log(Debug::Warning) << "No exclusive fullscreen mode at the desktop's: " << SDL_GetError();
            SDL_SetWindowFullscreen(window, true);
        }
        else if (windowMode == Settings::WindowMode::WindowedFullscreen)
        {
            SDL_SetWindowFullscreenMode(window, nullptr);
            SDL_SetWindowFullscreen(window, true);
        }
        else
        {
            SDL_SetWindowSize(window, width, height);
            SDL_SetWindowBordered(window, windowBorder);

            centerWindow(window);
        }
    }
}
