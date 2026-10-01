#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLVIDEOWRAPPER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLVIDEOWRAPPER_H

struct SDL_Window;

namespace Settings
{
    enum class WindowMode;
}

namespace SDLUtil
{
    /// The size in the display's points that gives `pixels` pixels at `density` pixels a point, in
    /// the exact ratio: a density of one and a half is not a whole number.
    int windowPoints(int pixels, float density);

    /// Puts `window` in `windowMode`: exclusive fullscreen at the display's desktop mode, borderless
    /// fullscreen on the whole display, or a window of `width` by `height` pixels centred on its
    /// display. Never another display mode: the frame's resolution is the renderer's, which scales
    /// it to whatever the window is. What a new window and a changed setting both go through.
    void setVideoMode(SDL_Window* window, int width, int height, Settings::WindowMode windowMode, bool windowBorder);
}

#endif
