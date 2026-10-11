#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLVIDEOWRAPPER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLVIDEOWRAPPER_H

#include <osg/Vec2i>
#include <osg/ref_ptr>

#include <SDL3/SDL_video.h>

#include "vsyncmode.hpp"

namespace osgViewer
{
    class Viewer;
}

namespace Settings
{
    enum class WindowMode;
}

namespace SDLUtil
{
    /// The display at `index`, or the primary display if there is none.
    SDL_DisplayID displayAt(int index);

    /// The size of `mode` in pixels. SDL gives display modes in points.
    osg::Vec2i modePixels(const SDL_DisplayMode& mode);

    int windowPoints(int pixels, float density);

    /// False for a hidden, minimized or occluded window.
    bool isWindowShown(SDL_WindowFlags flags);

    class VideoWrapper
    {
    public:
        VideoWrapper(SDL_Window* window, osg::ref_ptr<osgViewer::Viewer> viewer);
        ~VideoWrapper();

        void setSyncToVBlank(VSyncMode vsyncMode);

        /// Sets the mode of `window`, and its size in pixels unless it is borderless fullscreen.
        static void setVideoMode(
            SDL_Window* window, int width, int height, Settings::WindowMode windowMode, bool windowBorder);

        /// Sets the size of `window` in pixels.
        static void setWindowSize(SDL_Window* window, int width, int height);

        static void centerWindow(SDL_Window* window);

    private:
        SDL_Window* mWindow;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
    };

}

#endif
