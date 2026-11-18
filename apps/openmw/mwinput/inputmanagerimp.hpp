#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>

#include <apps/openmw/mwbase/inputmanager.hpp>
#include <components/sdlutil/events.hpp>
#include <components/settings/settings.hpp>

#include "actions.hpp"

namespace MWWorld
{
    class Player;
}

namespace MWBase
{
    class WindowManager;
}

namespace MWRender
{
    class Renderer;
}

namespace SDLUtil
{
    class InputWrapper;
}

namespace MWInput
{
    class ControlSwitch;
    class ActionManager;
    class BindingsManager;
    class ControllerManager;
    class KeyboardManager;
    class MouseManager;
    class SensorManager;
    class GyroManager;

    /**
     * @brief Class that provides a high-level API for game input
     */
    class InputManager final : public MWBase::InputManager
    {
    public:
        InputManager(MWRender::Renderer& renderer, std::unique_ptr<SDLUtil::InputWrapper> inputWrapper,
            const std::filesystem::path& userFile, bool userFileExists,
            const std::filesystem::path& userControllerBindingsFile,
            const std::filesystem::path& controllerBindingsFile);

        ~InputManager() final;

        /// Clear all savegame-specific data
        void clear() override;

        void update(float dt, bool disableControls, bool disableEvents = false) override;

        void changeInputMode(bool guiMode) override;

        void setCursorMode(MWBase::CursorMode mode) override;
        MWBase::CursorMode getCursorMode() const override;
        void setCursorPosition(const osg::Vec2f& position) override;
        osg::Vec2f getCursorPosition() const override;

        void processChangedSettings(const Settings::CategorySettingVector& changed) override;

        void setDragDrop(bool dragDrop) override;
        void setGamepadGuiCursorEnabled(bool enabled) override;
        bool isGamepadGuiCursorEnabled() override;

        void toggleControlSwitch(std::string_view sw, bool value) override;
        bool getControlSwitch(std::string_view sw) override;

        std::string_view getActionDescription(std::int32_t action) const override;
        std::string getActionKeyBindingName(std::int32_t action) const override;
        std::string getActionControllerBindingName(std::int32_t action) const override;
        bool actionIsActive(std::int32_t action) const override;

        float getActionValue(std::int32_t action) const override;
        bool isControllerButtonPressed(SDL_GamepadButton button) const override;
        float getControllerAxisValue(SDL_GamepadAxis axis) const override;
        float getMouseMoveX() const override;
        float getMouseMoveY() const override;
        void warpMouseToWidget(MyGUI::Widget* widget) override;

        std::int32_t getNumActions() override { return A_Last; }
        const std::initializer_list<std::int32_t>& getActionKeySorting() override;
        const std::initializer_list<std::int32_t>& getActionControllerSorting() override;
        void enableDetectingBindingMode(std::int32_t action, bool keyboard) override;
        void resetToDefaultKeyBindings() override;
        void resetToDefaultControllerBindings() override;

        void setJoystickLastUsed(bool enabled) override;
        bool joystickLastUsed() override;
        std::string getControllerButtonIcon(std::int32_t button) override;
        std::string getControllerAxisIcon(std::int32_t axis) override;

        std::size_t countSavedGameRecords() const override;
        void write(ESM::ESMWriter& writer, Loading::Listener& progress) override;
        void readRecord(ESM::ESMReader& reader, std::uint32_t type) override;

        void resetIdleTime() override;
        bool isIdle() const override;

        void executeAction(std::int32_t action) override;

        bool controlsDisabled() override { return mControlsDisabled; }

    private:
        bool mControlsDisabled;

        void saveBindings() override;

        std::unique_ptr<SDLUtil::InputWrapper> mInputWrapper;
        std::unique_ptr<BindingsManager> mBindingsManager;
        std::unique_ptr<ControlSwitch> mControlSwitch;
        std::unique_ptr<ActionManager> mActionManager;
        std::unique_ptr<KeyboardManager> mKeyboardManager;
        std::unique_ptr<MouseManager> mMouseManager;
        std::unique_ptr<ControllerManager> mControllerManager;
        std::unique_ptr<SensorManager> mSensorManager;
        std::unique_ptr<GyroManager> mGyroManager;
    };
}
