#pragma once

#include <cstdint>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>

////////////
// Events //
////////////

namespace SDLUtil
{

    /** Extended mouse event struct where we treat the wheel like an axis, like everyone expects */
    struct MouseMotionEvent : SDL_MouseMotionEvent
    {

        Sint32 zrel;
        Sint32 z;
    };

    struct TouchEvent
    {
        std::int32_t mDevice;
        std::int32_t mFinger;
        float mX;
        float mY;
        float mPressure;

        explicit TouchEvent(const SDL_GamepadTouchpadEvent& arg)
            : mDevice(arg.touchpad)
            , mFinger(arg.finger)
            , mX(arg.x)
            , mY(arg.y)
            , mPressure(arg.pressure)
        {
        }
    };

    ///////////////
    // Listeners //
    ///////////////

    class MouseListener
    {
    public:
        virtual ~MouseListener() = default;
        virtual void mouseMoved(const MouseMotionEvent& arg) = 0;
        virtual void mousePressed(const SDL_MouseButtonEvent& arg, Uint8 id) = 0;
        virtual void mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 id) = 0;
        virtual void mouseWheelMoved(const SDL_MouseWheelEvent& arg) = 0;
    };

    class SensorListener
    {
    public:
        virtual ~SensorListener() = default;
        virtual void sensorUpdated(const SDL_SensorEvent& arg) = 0;
        virtual void displayOrientationChanged() = 0;
    };

    class KeyListener
    {
    public:
        virtual ~KeyListener() = default;
        virtual void textInput(const SDL_TextInputEvent& arg) {}
        virtual void keyPressed(const SDL_KeyboardEvent& arg) = 0;
        virtual void keyReleased(const SDL_KeyboardEvent& arg) = 0;
    };

    class ControllerListener
    {
    public:
        virtual ~ControllerListener() = default;

        virtual void buttonPressed(std::int32_t deviceID, const SDL_GamepadButtonEvent& evt) = 0;
        virtual void buttonReleased(std::int32_t deviceID, const SDL_GamepadButtonEvent& evt) = 0;

        virtual void axisMoved(std::int32_t deviceID, const SDL_GamepadAxisEvent& arg) = 0;

        virtual void controllerAdded(std::int32_t deviceID, const SDL_GamepadDeviceEvent& arg) = 0;
        virtual void controllerRemoved(const SDL_GamepadDeviceEvent& arg) = 0;

        virtual void touchpadMoved(std::int32_t deviceId, const TouchEvent& arg) = 0;
        virtual void touchpadPressed(std::int32_t deviceId, const TouchEvent& arg) = 0;
        virtual void touchpadReleased(std::int32_t deviceId, const TouchEvent& arg) = 0;
    };

    class WindowListener
    {
    public:
        virtual ~WindowListener() = default;

        /** @remarks The window's visibility changed */
        virtual void windowVisibilityChange(bool visible) {}

        virtual void windowClosed() {}

        /** @remarks The window's drawable is now x by y pixels */
        virtual void windowResized(std::int32_t x, std::int32_t y) {}

        /** @remarks The window is now width by height in the desktop's unit, which `SDL_GetWindowSize`
         * answers: points on Wayland, pixels on Windows and X11 */
        virtual void windowSized(std::int32_t width, std::int32_t height) {}

        /// The window went to another display, or its display's desktop mode or scale changed.
        virtual void windowDisplayChanged() {}
    };

}
