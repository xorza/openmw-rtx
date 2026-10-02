#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLDISPLAY_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLDISPLAY_H

#include <vector>

#include <SDL3/SDL_video.h>

namespace Settings
{
    enum class WindowMode;
}

namespace SDLUtil
{
    /// The display `[Video] screen` names: SDL3 names a display by an ID, and the setting keeps the
    /// display's place in SDL's list. The primary display where the list has no such place, since a
    /// display unplugged since the setting was written is no reason not to start.
    SDL_DisplayID displayAt(int index);

    struct DisplayResolution
    {
        int mWidth = 0;
        int mHeight = 0;
    };

    /// The sizes in pixels of `display`'s fullscreen modes, largest first and each once: SDL lists a
    /// mode per refresh rate and per pixel density.
    std::vector<DisplayResolution> displayResolutions(SDL_DisplayID display);

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
