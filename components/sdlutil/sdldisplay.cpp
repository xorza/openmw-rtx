#include "sdldisplay.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

#include <SDL3/SDL_stdinc.h>

namespace SDLUtil
{
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
}
