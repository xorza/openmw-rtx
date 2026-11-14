#include "skykeys.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace RtxTool
{
    SkyPress SkyKeys::listen()
    {
        const bool* const keys = SDL_GetKeyboardState(nullptr);
        const bool on = mOn.wentDown(keys[SDL_SCANCODE_RIGHTBRACKET]);
        const bool back = mBack.wentDown(keys[SDL_SCANCODE_LEFTBRACKET]);
        return SkyPress{
            .mSteps = (on ? 1 : 0) - (back ? 1 : 0),
            .mAtOnce = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
        };
    }
}
