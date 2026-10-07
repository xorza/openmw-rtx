#include "sdlvideowrapper.hpp"

#include <cmath>
#include <optional>

#include <components/debug/debuglog.hpp>
#include <components/sdlutil/sdlgraphicswindow.hpp>
#include <components/settings/settings.hpp>

#include <osgViewer/Viewer>

#include <SDL3/SDL_video.h>

namespace SDLUtil
{
    namespace
    {
        // SDL lists a mode per refresh rate and per pixel density, so a mode matches by its size in pixels, at
        // the refresh rate nearest the desktop's.
        std::optional<SDL_DisplayMode> findFullscreenMode(SDL_DisplayID display, int width, int height)
        {
            const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
            const float desktopRate = desktop != nullptr ? desktop->refresh_rate : 0.f;

            std::optional<SDL_DisplayMode> found;
            int count = 0;
            SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(display, &count);
            for (int i = 0; i < count; ++i)
            {
                const SDL_DisplayMode& mode = *modes[i];
                if (modePixels(mode) != osg::Vec2i(width, height))
                    continue;
                if (!found || std::abs(mode.refresh_rate - desktopRate) < std::abs(found->refresh_rate - desktopRate))
                    found = mode;
            }
            SDL_free(modes);

            if (found)
                return found;

            SDL_DisplayMode closest;
            if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0.f, false, &closest))
                return closest;
            return std::nullopt;
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

    osg::Vec2i modePixels(const SDL_DisplayMode& mode)
    {
        return osg::Vec2i(static_cast<int>(std::lround(mode.w * mode.pixel_density)),
            static_cast<int>(std::lround(mode.h * mode.pixel_density)));
    }

    int windowPoints(int pixels, float density)
    {
        return static_cast<int>(std::lround(pixels / density));
    }

    VideoWrapper::VideoWrapper(SDL_Window* window, osg::ref_ptr<osgViewer::Viewer> viewer)
        : mWindow(window)
        , mViewer(std::move(viewer))
    {
    }

    VideoWrapper::~VideoWrapper()
    {
        // Only exclusive fullscreen needs an early display-mode reset. Leaving desktop fullscreen here
        // can bring a background window to the foreground on macOS; let window destruction handle it.
        if ((SDL_GetWindowFlags(mWindow) & SDL_WINDOW_FULLSCREEN) && SDL_GetWindowFullscreenMode(mWindow) != nullptr)
            SDL_SetWindowFullscreen(mWindow, false);
    }

    void VideoWrapper::setSyncToVBlank(VSyncMode vsyncMode)
    {
        osgViewer::Viewer::Windows windows;
        mViewer->getWindows(windows);
        mViewer->stopThreading();
        for (osgViewer::Viewer::Windows::iterator it = windows.begin(); it != windows.end(); ++it)
        {
            osgViewer::GraphicsWindow* win = *it;
            if (GraphicsWindowSDL2* sdl2win = dynamic_cast<GraphicsWindowSDL2*>(win))
                sdl2win->setSyncToVBlank(vsyncMode);
            else
                win->setSyncToVBlank(vsyncMode != VSyncMode::Disabled);
        }
        mViewer->startThreading();
    }

    void VideoWrapper::setVideoMode(
        SDL_Window* window, int width, int height, Settings::WindowMode windowMode, bool windowBorder)
    {
        SDL_SetWindowFullscreen(window, false);

        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED)
            SDL_RestoreWindow(window);

        if (windowMode == Settings::WindowMode::Fullscreen)
        {
            const std::optional<SDL_DisplayMode> mode
                = findFullscreenMode(SDL_GetDisplayForWindow(window), width, height);
            if (!mode || !SDL_SetWindowFullscreenMode(window, &*mode))
                Log(Debug::Warning) << "Couldn't set a " << width << "x" << height
                                    << " fullscreen mode: " << SDL_GetError();
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
            // centerWindow reads the size back, and a window system can apply it asynchronously.
            SDL_SyncWindow(window);

            centerWindow(window);
        }
    }

    void VideoWrapper::centerWindow(SDL_Window* window)
    {
        // Resize breaks the sdl window in some cases; see issue: #5539
        SDL_Rect rect{};
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &rect);
        SDL_GetWindowSize(window, &w, &h);

        x = rect.x;
        y = rect.y;

        // Center dimensions that do not fill the screen
        if (w < rect.w)
        {
            x = rect.x + rect.w / 2 - w / 2;
        }
        if (h < rect.h)
        {
            y = rect.y + rect.h / 2 - h / 2;
        }

        SDL_SetWindowPosition(window, x, y);
    }

}
