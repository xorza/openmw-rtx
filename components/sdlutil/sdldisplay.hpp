#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLDISPLAY_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLDISPLAY_H

#include <vector>

#include <SDL3/SDL_video.h>

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
}

#endif
