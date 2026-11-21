#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/inputmanager.hpp>
#include <apps/openmw/mwinput/actions.hpp>
#include <apps/openmw/mwinput/bindingsmanager.hpp>
#include <components/testing/util.hpp>

namespace MWInput
{
    namespace
    {
        /// The input manager the bindings report a changed action to, which takes it and does nothing.
        class QuietInput final : public MWBase::InputManager
        {
        public:
            void clear() override {}
            void update(float, bool, bool) override {}
            void changeInputMode(bool) override {}
            void setCursorMode(MWBase::CursorMode) override {}
            MWBase::CursorMode getCursorMode() const override { return MWBase::CursorMode::Locked; }
            void setCursorPosition(const osg::Vec2f&) override {}
            osg::Vec2f getCursorPosition() const override { return {}; }
            void processChangedSettings(const std::set<std::pair<std::string, std::string>>&) override {}
            void setDragDrop(bool) override {}
            bool isGamepadGuiCursorEnabled() override { return false; }
            void setGamepadGuiCursorEnabled(bool) override {}
            void toggleControlSwitch(std::string_view, bool) override {}
            bool getControlSwitch(std::string_view) override { return true; }
            std::string_view getActionDescription(std::int32_t) const override { return {}; }
            std::string getActionKeyBindingName(std::int32_t) const override { return {}; }
            std::string getActionControllerBindingName(std::int32_t) const override { return {}; }
            bool actionIsActive(std::int32_t) const override { return false; }
            float getActionValue(std::int32_t) const override { return 0.0f; }
            bool isControllerButtonPressed(SDL_GamepadButton) const override { return false; }
            float getControllerAxisValue(SDL_GamepadAxis) const override { return 0.0f; }
            float getMouseMoveX() const override { return 0.0f; }
            float getMouseMoveY() const override { return 0.0f; }
            void warpMouseToWidget(MyGUI::Widget*) override {}
            const std::initializer_list<std::int32_t>& getActionKeySorting() override { return mNone; }
            const std::initializer_list<std::int32_t>& getActionControllerSorting() override { return mNone; }
            std::int32_t getNumActions() override { return A_Last; }
            void enableDetectingBindingMode(std::int32_t, bool) override {}
            void resetToDefaultKeyBindings() override {}
            void resetToDefaultControllerBindings() override {}
            bool joystickLastUsed() override { return false; }
            void setJoystickLastUsed(bool) override {}
            std::string getControllerButtonIcon(std::int32_t) override { return {}; }
            std::string getControllerAxisIcon(std::int32_t) override { return {}; }
            std::size_t countSavedGameRecords() const override { return 0; }
            void write(ESM::ESMWriter&, Loading::Listener&) override {}
            void readRecord(ESM::ESMReader&, std::uint32_t) override {}
            void resetIdleTime() override {}
            bool isIdle() const override { return false; }
            void executeAction(std::int32_t) override {}
            bool controlsDisabled() override { return false; }
            void saveBindings() override {}

        private:
            std::initializer_list<std::int32_t> mNone;
        };

        void moveAxis(BindingsManager& bindings, SDL_GamepadAxis axis, Sint16 value)
        {
            SDL_GamepadAxisEvent event{};
            event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
            event.axis = static_cast<Uint8>(axis);
            event.value = value;
            // The device the input wrapper names every controller by.
            bindings.controllerAxisMoved(1, event);
        }

        /// **A menu's close tells the controls each axis that moved while it kept their events, and no other**
        /// (upstream #8863). The right stick held full right turns the camera at 1, the look action's top; the menu
        /// keeps the stick's way back, and the action stays at 1. Caught up with the stick where they heard it, nothing
        /// moves; caught up with it back at rest, the action stands at 0.5, the axis's centre, which turns nothing. The
        /// left stick, never heard, pushed full forward while the menu was open, reaches its action too: the forward
        /// action at its bottom, 0, since the stick's up is its negative.
        TEST(BindingsManagerTest, aMenusCloseTellsTheControlsEachAxisThatMovedWhileItKeptTheirEvents)
        {
            MWBase::Environment environment;
            QuietInput input;
            environment.setInputManager(input);
            BindingsManager bindings(TestingOpenMW::outputFilePath("bindings-catch-up.cfg"), false);

            moveAxis(bindings, SDL_GAMEPAD_AXIS_RIGHTX, SDL_JOYSTICK_AXIS_MAX);
            ASSERT_EQ(bindings.getActionValue(A_LookLeftRight), 1.0f);
            ASSERT_EQ(bindings.getActionValue(A_MoveForwardBackward), 0.5f);

            std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> standing{};
            standing[SDL_GAMEPAD_AXIS_RIGHTX] = SDL_JOYSTICK_AXIS_MAX;
            bindings.catchUpControllerAxes(standing);
            EXPECT_EQ(bindings.getActionValue(A_LookLeftRight), 1.0f) << "nothing moved, so nothing is told";
            EXPECT_EQ(bindings.getActionValue(A_MoveForwardBackward), 0.5f);

            standing[SDL_GAMEPAD_AXIS_RIGHTX] = 0;
            standing[SDL_GAMEPAD_AXIS_LEFTY] = SDL_JOYSTICK_AXIS_MIN;
            bindings.catchUpControllerAxes(standing);
            EXPECT_EQ(bindings.getActionValue(A_LookLeftRight), 0.5f);
            EXPECT_EQ(bindings.getActionValue(A_MoveForwardBackward), 0.0f);
        }
    }
}
