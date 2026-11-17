#pragma once

#include <cstdint>
#include <string>

#include <MyGUI_KeyCode.h>
#include <SDL3/SDL_keycode.h>

namespace MyGUI
{
    struct MouseButton;
}

namespace SDLUtil
{
    std::string sdlControllerButtonToString(std::int32_t button);

    std::string sdlControllerAxisToString(std::int32_t axis);

    MyGUI::MouseButton sdlMouseButtonToMyGui(Uint8 button);
    Uint8 myGuiMouseButtonToSdl(MyGUI::MouseButton button);

    MyGUI::KeyCode sdlKeyToMyGUI(SDL_Keycode code);
    SDL_Keycode myGuiKeyToSdl(MyGUI::KeyCode button);
}
