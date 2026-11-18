#pragma once

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include <SDL3/SDL_gamepad.h>
#include <osg/Vec2f>

namespace Loading
{
    class Listener;
}

namespace ESM
{
    class ESMReader;
    class ESMWriter;
}

namespace MyGUI
{
    class Widget;
}

namespace MWBase
{
    enum class CursorMode
    {
        Confined,
        Free,
        Locked,
    };

    /// \brief Interface for input manager (implemented in MWInput)
    class InputManager
    {
        ///< not implemented

        ///< not implemented

    public:
        InputManager(const InputManager&) = delete;
        InputManager& operator=(const InputManager&) = delete;
        InputManager() {}

        /// Clear all savegame-specific data
        virtual void clear() = 0;

        virtual ~InputManager() {}

        virtual void update(float dt, bool disableControls, bool disableEvents = false) = 0;

        virtual void changeInputMode(bool guiMode) = 0;

        virtual void setCursorMode(CursorMode mode) = 0;
        virtual CursorMode getCursorMode() const = 0;
        virtual void setCursorPosition(const osg::Vec2f& position) = 0;
        virtual osg::Vec2f getCursorPosition() const = 0;

        virtual void processChangedSettings(const std::set<std::pair<std::string, std::string>>& changed) = 0;

        virtual void setDragDrop(bool dragDrop) = 0;
        virtual bool isGamepadGuiCursorEnabled() = 0;
        virtual void setGamepadGuiCursorEnabled(bool enabled) = 0;

        virtual void toggleControlSwitch(std::string_view sw, bool value) = 0;
        virtual bool getControlSwitch(std::string_view sw) = 0;

        virtual std::string_view getActionDescription(std::int32_t action) const = 0;
        virtual std::string getActionKeyBindingName(std::int32_t action) const = 0;
        virtual std::string getActionControllerBindingName(std::int32_t action) const = 0;
        virtual bool actionIsActive(std::int32_t action) const = 0;

        virtual float getActionValue(std::int32_t action) const = 0; // returns value in range [0, 1]
        virtual bool isControllerButtonPressed(SDL_GamepadButton button) const = 0;
        virtual float getControllerAxisValue(SDL_GamepadAxis axis) const = 0; // returns value in range [-1, 1]
        virtual float getMouseMoveX() const = 0;
        virtual float getMouseMoveY() const = 0;
        virtual void warpMouseToWidget(MyGUI::Widget* widget) = 0;

        /// Actions available for binding to keyboard buttons
        virtual const std::initializer_list<std::int32_t>& getActionKeySorting() = 0;
        /// Actions available for binding to controller buttons
        virtual const std::initializer_list<std::int32_t>& getActionControllerSorting() = 0;
        virtual std::int32_t getNumActions() = 0;
        /// If keyboard is true, only pay attention to keyboard events. If false, only pay attention to controller
        /// events (excluding esc)
        virtual void enableDetectingBindingMode(std::int32_t action, bool keyboard) = 0;
        virtual void resetToDefaultKeyBindings() = 0;
        virtual void resetToDefaultControllerBindings() = 0;

        /// Returns if the last used input device was a joystick or a keyboard
        /// @return true if joystick, false otherwise
        virtual bool joystickLastUsed() = 0;
        virtual void setJoystickLastUsed(bool enabled) = 0;
        virtual std::string getControllerButtonIcon(std::int32_t button) = 0;
        virtual std::string getControllerAxisIcon(std::int32_t axis) = 0;

        virtual std::size_t countSavedGameRecords() const = 0;
        virtual void write(ESM::ESMWriter& writer, Loading::Listener& progress) = 0;
        virtual void readRecord(ESM::ESMReader& reader, std::uint32_t type) = 0;

        virtual void resetIdleTime() = 0;
        virtual bool isIdle() const = 0;

        virtual void executeAction(std::int32_t action) = 0;

        virtual bool controlsDisabled() = 0;

        virtual void saveBindings() = 0;
    };
}
