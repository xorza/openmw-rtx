#include "skykeys.hpp"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_keycode.h>
#include <SDL3/SDL_scancode.h>

namespace RtxTool
{
    SkyPress SkyKeys::listen()
    {
        const bool* const keys = SDL_GetKeyboardState(nullptr);
        const bool back = keys[SDL_SCANCODE_LEFTBRACKET];
        const bool on = keys[SDL_SCANCODE_RIGHTBRACKET];

        const SkyPress press{
            .mSteps = (on && !mOnHeld ? 1 : 0) - (back && !mBackHeld ? 1 : 0),
            .mAtOnce = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0,
        };
        mBackHeld = back;
        mOnHeld = on;

        return press;
    }
}
