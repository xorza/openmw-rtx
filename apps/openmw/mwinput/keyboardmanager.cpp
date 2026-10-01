#include "keyboardmanager.hpp"

#include <cctype>

#include <MyGUI_InputManager.h>

#include <components/sdlutil/sdlmappings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/inputmanager.hpp"
#include "../mwbase/luamanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "actions.hpp"
#include "bindingsmanager.hpp"

namespace MWInput
{
    KeyboardManager::KeyboardManager(BindingsManager* bindingsManager, SDL_Window* window)
        : mBindingsManager(bindingsManager)
        , mWindow(window)
    {
    }

    void KeyboardManager::textInput(const SDL_TextInputEvent& arg)
    {
        MyGUI::UString ustring(&arg.text[0]);
        MyGUI::UString::utf32string utf32string = ustring.asUTF32();
        for (MyGUI::UString::utf32string::const_iterator it = utf32string.begin(); it != utf32string.end(); ++it)
            MyGUI::InputManager::getInstance().injectKeyPress(MyGUI::KeyCode::None, *it);
    }

    void KeyboardManager::keyPressed(const SDL_KeyboardEvent& arg)
    {
        // HACK: to make default keybinding for the console work without printing an extra "^" upon closing
        // This assumes that SDL_TextInput events always come *after* the key event
        // (which is somewhat reasonable, and hopefully true for all SDL platforms)
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.key);
        if (mBindingsManager->getKeyBinding(A_Console) == arg.scancode
            // HACK: allow upper case variant of console keybinding.
            && (arg.mod & SDL_KMOD_SHIFT) == 0 && MWBase::Environment::get().getWindowManager()->isConsoleMode())
            SDL_StopTextInput(mWindow);

        bool consumed = SDL_TextInputActive(mWindow) && // Little trick to check if key is printable
            (!(SDLK_SCANCODE_MASK & arg.key) &&
                // Don't trust isprint for symbols outside the extended ASCII range
                ((kc == MyGUI::KeyCode::None && arg.key > 0xff)
                    || (arg.key <= 255 && std::isprint(static_cast<int>(arg.key)))));
        if (kc != MyGUI::KeyCode::None && !mBindingsManager->isDetectingBindingState())
        {
            if (MWBase::Environment::get().getWindowManager()->injectKeyPress(kc, 0, arg.repeat))
                consumed = true;
            mBindingsManager->setPlayerControlsEnabled(!consumed);
        }

        if (arg.repeat)
            return;

        MWBase::InputManager* input = MWBase::Environment::get().getInputManager();
        if (!input->controlsDisabled() && !consumed)
            mBindingsManager->keyPressed(arg);

        if (!consumed)
        {
            MWBase::Environment::get().getLuaManager()->inputEvent({ MWBase::LuaManager::InputEvent::KeyPressed, arg });
        }

        input->setJoystickLastUsed(false);
    }

    void KeyboardManager::keyReleased(const SDL_KeyboardEvent& arg)
    {
        MWBase::Environment::get().getInputManager()->setJoystickLastUsed(false);
        auto kc = SDLUtil::sdlKeyToMyGUI(arg.key);

        if (!mBindingsManager->isDetectingBindingState())
            mBindingsManager->setPlayerControlsEnabled(!MyGUI::InputManager::getInstance().injectKeyRelease(kc));
        mBindingsManager->keyReleased(arg);
        MWBase::Environment::get().getLuaManager()->inputEvent({ MWBase::LuaManager::InputEvent::KeyReleased, arg });
    }
}
