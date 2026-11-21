#include <array>
#include <string_view>

#include <gtest/gtest.h>

#include <SDL3/SDL_keycode.h>

#include <components/sdlutil/sdlmappings.hpp>

namespace SDLUtil
{
    namespace
    {
        /// **A key types text where it puts a character in a field, and NumLock decides the keypad's digits**: with it
        /// on, keypad 8 types "8" and a field takes it, so it turns no look action, which a menu took as the wheel and
        /// moved a slider with (upstream #8552); with it off, keypad 8 is the arrow up and types nothing. The keypad's
        /// operators type with NumLock either way, its Enter never; a letter types, a function key and an arrow do
        /// not, and a character past Latin-1 that MyGUI has no key for types.
        TEST(SDLUtilMappingsTest, aKeyTypesTextWhereItPutsACharacterInAField)
        {
            struct Case
            {
                std::string_view mName;
                SDL_Keycode mKey;
                SDL_Keymod mMod;
                bool mTypes;
            };
            constexpr std::array cases{
                Case{ "keypad 8, NumLock on", SDLK_KP_8, SDL_KMOD_NUM, true },
                Case{ "keypad 8, NumLock off", SDLK_KP_8, SDL_KMOD_NONE, false },
                Case{ "keypad 2, NumLock and shift", SDLK_KP_2, SDL_KMOD_NUM | SDL_KMOD_SHIFT, true },
                Case{ "keypad point, NumLock on", SDLK_KP_PERIOD, SDL_KMOD_NUM, true },
                Case{ "keypad point, NumLock off", SDLK_KP_PERIOD, SDL_KMOD_NONE, false },
                Case{ "keypad minus, NumLock off", SDLK_KP_MINUS, SDL_KMOD_NONE, true },
                Case{ "keypad plus, NumLock on", SDLK_KP_PLUS, SDL_KMOD_NUM, true },
                Case{ "keypad Enter, NumLock on", SDLK_KP_ENTER, SDL_KMOD_NUM, false },
                Case{ "8", SDLK_8, SDL_KMOD_NONE, true },
                Case{ "a", SDLK_A, SDL_KMOD_NONE, true },
                Case{ "F1", SDLK_F1, SDL_KMOD_NUM, false },
                Case{ "arrow up", SDLK_UP, SDL_KMOD_NUM, false },
                Case{ "Cyrillic a", 0x430, SDL_KMOD_NONE, true },
            };
            for (const Case& expected : cases)
            {
                SCOPED_TRACE(expected.mName);
                EXPECT_EQ(typesText(expected.mKey, expected.mMod), expected.mTypes);
            }
        }
    }
}
