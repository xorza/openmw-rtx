#include "skykeys.hpp"

#include <SDL_keyboard.h>
#include <SDL_scancode.h>

namespace RtxTool
{
    int SkyKeys::listen()
    {
        const Uint8* const keys = SDL_GetKeyboardState(nullptr);
        const bool back = keys[SDL_SCANCODE_LEFTBRACKET] != 0;
        const bool on = keys[SDL_SCANCODE_RIGHTBRACKET] != 0;

        const int steps = (on && !mOnHeld ? 1 : 0) - (back && !mBackHeld ? 1 : 0);
        mBackHeld = back;
        mOnHeld = on;

        return steps;
    }
}
