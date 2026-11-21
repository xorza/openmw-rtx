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

    /// **Whether a key with modifiers `mod` types a character**, which a text field takes in place of the game: a
    /// printable key, and the keypad's digits and point with NumLock on, which type them, where with NumLock off they
    /// stand for the arrows and the keys around them (SDL's `hide_numpad`, `SDL_ConvertNumpadKeycode`). The keypad's
    /// operators type whatever NumLock says.
    bool typesText(SDL_Keycode key, SDL_Keymod mod);
    SDL_Keycode myGuiKeyToSdl(MyGUI::KeyCode button);
}
