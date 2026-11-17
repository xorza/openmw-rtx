#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <components/sdlutil/events.hpp>

namespace MWInput
{
    class BindingsListener;
    class InputControlSystem;

    class BindingsManager
    {
    public:
        BindingsManager(const std::filesystem::path& userFile, bool userFileExists);

        virtual ~BindingsManager();

        std::string_view getActionDescription(std::int32_t action);
        std::string getActionKeyBindingName(std::int32_t action);
        std::string getActionControllerBindingName(std::int32_t action);
        const std::initializer_list<std::int32_t>& getActionKeySorting();
        const std::initializer_list<std::int32_t>& getActionControllerSorting();

        void enableDetectingBindingMode(std::int32_t action, bool keyboard);
        bool isDetectingBindingState() const;

        void loadKeyDefaults(bool force = false);
        void loadControllerDefaults(bool force = false);

        void setDragDrop(bool dragDrop);

        void update(float dt);

        void setPlayerControlsEnabled(bool enabled);

        void setJoystickDeadZone(float deadZone);

        bool isLeftOrRightButton(std::int32_t action, bool joystick) const;

        bool actionIsActive(std::int32_t id) const;
        float getActionValue(std::int32_t id) const; // returns value in range [0, 1]

        SDL_Gamepad* getControllerOrNull() const;

        void mousePressed(const SDL_MouseButtonEvent& arg, Uint8 deviceID);
        void mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 deviceID);
        void mouseMoved(const SDLUtil::MouseMotionEvent& arg);
        void mouseWheelMoved(const SDL_MouseWheelEvent& arg);

        void keyPressed(const SDL_KeyboardEvent& arg);
        void keyReleased(const SDL_KeyboardEvent& arg);

        void controllerAdded(std::int32_t deviceID, const SDL_GamepadDeviceEvent& arg);
        void controllerRemoved(const SDL_GamepadDeviceEvent& arg);
        void controllerButtonPressed(std::int32_t deviceID, const SDL_GamepadButtonEvent& arg);
        void controllerButtonReleased(std::int32_t deviceID, const SDL_GamepadButtonEvent& arg);
        void controllerAxisMoved(std::int32_t deviceID, const SDL_GamepadAxisEvent& arg);

        SDL_Scancode getKeyBinding(std::int32_t actionId);

        void actionValueChanged(std::int32_t action, float currentValue, float previousValue);

        void saveBindings();

    private:
        void setupSDLKeyMappings();

        std::unique_ptr<InputControlSystem> mInputBinder;
        std::unique_ptr<BindingsListener> mListener;

        std::filesystem::path mUserFile;

        bool mDragDrop;
    };
}
