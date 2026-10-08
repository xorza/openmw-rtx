#pragma once

#include <memory>

#include <SDL3/SDL_video.h>
#include <osg/ref_ptr>

#include <components/sdlutil/sdlgraphicswindow.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace MWRender
{
    /// The SDL window the rasterizer draws into and the GL context made on it, from the video
    /// settings: the antialiasing is stepped down until the system grants a window and a context
    /// with as many samples as it asked. The context is closed before the window goes, and both go
    /// after the viewer, before which this is declared in whatever owns all three.
    class GlWindow
    {
    public:
        /// Throws where SDL makes no window or no context at any antialiasing.
        explicit GlWindow(SDLUtil::VSyncMode vsync);
        ~GlWindow();

        GlWindow(const GlWindow&) = delete;
        GlWindow& operator=(const GlWindow&) = delete;

        SDL_Window* get() const { return mWindow.get(); }
        SDLUtil::GraphicsWindowSDL& getGraphics() const { return *mGraphics; }

    private:
        std::unique_ptr<SDL_Window, void (*)(SDL_Window*)> mWindow{ nullptr, SDL_DestroyWindow };

        /// After the window, so a constructor that throws lets the context go before the window.
        osg::ref_ptr<SDLUtil::GraphicsWindowSDL> mGraphics;
    };
}
