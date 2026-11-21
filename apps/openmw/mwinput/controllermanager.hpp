#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include <components/sdlutil/events.hpp>
#include <components/settings/settings.hpp>

namespace MWInput
{
    class BindingsManager;
    class MouseManager;

    class ControllerManager : public SDLUtil::ControllerListener
    {
    public:
        ControllerManager(BindingsManager* bindingsManager, MouseManager* mouseManager, SDL_Window* window,
            const std::filesystem::path& userControllerBindingsFile,
            const std::filesystem::path& controllerBindingsFile);

        ~ControllerManager() override = default;

        void update(float dt);

        void buttonPressed(std::int32_t deviceID, const SDL_GamepadButtonEvent& arg) override;
        void buttonReleased(std::int32_t deviceID, const SDL_GamepadButtonEvent& arg) override;
        void axisMoved(std::int32_t deviceID, const SDL_GamepadAxisEvent& arg) override;
        void controllerAdded(std::int32_t deviceID, const SDL_GamepadDeviceEvent& arg) override;
        void controllerRemoved(const SDL_GamepadDeviceEvent& arg) override;

        void touchpadMoved(std::int32_t deviceId, const SDLUtil::TouchEvent& arg) override;
        void touchpadPressed(std::int32_t deviceId, const SDLUtil::TouchEvent& arg) override;
        void touchpadReleased(std::int32_t deviceId, const SDLUtil::TouchEvent& arg) override;

        void setJoystickLastUsed(bool enabled) { mJoystickLastUsed = enabled; }
        bool joystickLastUsed() const { return mJoystickLastUsed; }

        void setGuiCursorEnabled(bool enabled) { mGuiCursorEnabled = enabled; }

        void setGamepadGuiCursorEnabled(bool enabled) { mGamepadGuiCursorEnabled = enabled; }
        bool gamepadGuiCursorEnabled() const { return mGamepadGuiCursorEnabled; }

        float getAxisValue(SDL_GamepadAxis axis) const; // returns value in range [-1, 1]

        /// Tells the controls where the controller's axes stand, once a menu that kept their events is gone
        /// (`BindingsManager::catchUpControllerAxes`); nothing with no controller.
        void catchUpAxes();
        bool isButtonPressed(SDL_GamepadButton button) const;

        bool isGyroAvailable() const;
        std::array<float, 3> getGyroValues() const;

        std::string getControllerButtonIcon(std::int32_t button);
        std::string getControllerAxisIcon(std::int32_t axis);

    private:
        // Return true if GUI consumes input.
        bool gamepadToGuiControl(const SDL_GamepadButtonEvent& arg);
        bool gamepadToGuiControl(const SDL_GamepadAxisEvent& arg);

        void enableGyroSensor();

        std::int32_t getControllerType();

        BindingsManager* mBindingsManager;
        MouseManager* mMouseManager;
        SDL_Window* mWindow;

        bool mGyroAvailable;
        bool mGamepadGuiCursorEnabled;
        bool mGuiCursorEnabled;
        bool mJoystickLastUsed;
        bool mGamepadMousePressed;
        bool mLeftTriggerGuiPressed;
        bool mRightTriggerGuiPressed;
    };
}
