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
    /// The display at `index` in SDL's list of displays, or the primary display if there is none at `index`.
    SDL_DisplayID displayAt(int index);

    /// The size of `mode` in pixels, as the resolution settings give it. SDL gives display modes in points.
    osg::Vec2i modePixels(const SDL_DisplayMode& mode);

    /// The size in a window's points that gives `pixels` pixels at `density` pixels per point.
    int windowPoints(int pixels, float density);

    class VideoWrapper
    {
    public:
        VideoWrapper(SDL_Window* window, osg::ref_ptr<osgViewer::Viewer> viewer);
        ~VideoWrapper();

        void setSyncToVBlank(VSyncMode vsyncMode);

        /// Puts `window` in `windowMode`, at `width` by `height` pixels unless it is borderless fullscreen.
        static void setVideoMode(
            SDL_Window* window, int width, int height, Settings::WindowMode windowMode, bool windowBorder);

        static void centerWindow(SDL_Window* window);

    private:
        SDL_Window* mWindow;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
    };

}

#endif
