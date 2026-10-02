#include "sdldisplay.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <utility>

#include <SDL3/SDL_stdinc.h>

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
            int w = 0;
            int h = 0;
            SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &rect);
            SDL_GetWindowSize(window, &w, &h);

            int x = rect.x;
            int y = rect.y;

            if (w < rect.w)
                x = rect.x + rect.w / 2 - w / 2;
            if (h < rect.h)
                y = rect.y + rect.h / 2 - h / 2;

            SDL_SetWindowPosition(window, x, y);
        }
    }

    SDL_DisplayID displayAt(int index)
    {
        int count = 0;
        SDL_DisplayID* displays = SDL_GetDisplays(&count);
        const SDL_DisplayID display = index >= 0 && index < count ? displays[index] : SDL_GetPrimaryDisplay();
        SDL_free(displays);
        return display;
    }

    std::vector<DisplayResolution> displayResolutions(SDL_DisplayID display)
    {
        std::vector<DisplayResolution> resolutions;
        int count = 0;
        SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
        resolutions.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const SDL_DisplayMode& mode = *modes[i];
            resolutions.push_back({ .mWidth = static_cast<int>(static_cast<float>(mode.w) * mode.pixel_density),
                .mHeight = static_cast<int>(static_cast<float>(mode.h) * mode.pixel_density) });
        }
        SDL_free(modes);

        const auto key = [](const DisplayResolution& r) { return std::pair(r.mWidth, r.mHeight); };
        std::ranges::sort(resolutions, std::ranges::greater{}, key);
        const auto [first, last] = std::ranges::unique(resolutions, std::ranges::equal_to{}, key);
        resolutions.erase(first, last);
        return resolutions;
    }

    int windowPoints(int pixels, float density)
    {
        return static_cast<int>(std::lround(static_cast<double>(pixels) / static_cast<double>(density)));
    }

    void setVideoMode(SDL_Window* window, int width, int height, Settings::WindowMode windowMode, bool windowBorder)
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
            const float density = SDL_GetWindowPixelDensity(window);
            SDL_SetWindowSize(window, windowPoints(width, density), windowPoints(height, density));
            SDL_SetWindowBordered(window, windowBorder);

            centerWindow(window);
        }
    }
}
