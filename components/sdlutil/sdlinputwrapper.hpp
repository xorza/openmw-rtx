#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H

#include <SDL3/SDL_events.h>

#include "events.hpp"

namespace SDLUtil
{
    class GraphicsListener;
    /// \brief A wrapper around SDL's event queue, mostly used for handling input-related events.
    class InputWrapper
    {
    public:
        /// @param graphics whoever draws into the window, told the moments that are its own
        InputWrapper(SDL_Window* window, GraphicsListener& graphics, bool grab);
        ~InputWrapper();

        void setMouseEventCallback(MouseListener* listen) { mMouseListener = listen; }
        void setSensorEventCallback(SensorListener* listen) { mSensorListener = listen; }
        void setKeyboardEventCallback(KeyListener* listen) { mKeyboardListener = listen; }
        void setWindowEventCallback(WindowListener* listen) { mWindowListener = listen; }
        void setControllerEventCallback(ControllerListener* listen) { mConListener = listen; }

        void capture(bool windowEventsOnly);
        bool isModifierHeld(int mod);
        bool isKeyDown(SDL_Scancode key);

        void setMouseVisible(bool visible);
        void setMouseRelative(bool relative);
        bool getMouseRelative() { return mMouseRelative; }
        void setGrabPointer(bool grab);

        /// Moves the pointer to a point in the window's pixels, the space the motion events report.
        void warpMouse(float x, float y);

        void updateMouseSettings();

    private:
        void handleWindowEvent(const SDL_Event& evt);
        void warpInWindow(float x, float y);

        bool _handleWarpMotion(const SDL_MouseMotionEvent& evt);
        void _wrapMousePointer(const SDL_MouseMotionEvent& evt);
        MouseMotionEvent _packageMouseMotion(const SDL_Event& evt);
        void _setWindowScale();

        SDL_Window* mSDLWindow;
        GraphicsListener& mGraphics;

        MouseListener* mMouseListener;
        SensorListener* mSensorListener;
        KeyListener* mKeyboardListener;
        WindowListener* mWindowListener;
        ControllerListener* mConListener;

        float mWarpX;
        float mWarpY;
        bool mWarpCompensate;
        bool mWrapPointer;

        bool mAllowGrab;
        bool mWantMouseVisible;
        bool mWantGrab;
        bool mWantRelative;
        bool mGrabPointer;
        bool mMouseRelative;

        bool mFirstMouseMove;

        Sint32 mMouseZ;
        double mPendingWheelY;

        bool mWindowHasFocus;
        bool mMouseInWindow;

        float mPixelDensity;
    };

}

#endif
