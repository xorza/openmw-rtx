#ifndef OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H
#define OPENMW_COMPONENTS_SDLUTIL_SDLINPUTWRAPPER_H

#include <osg/ref_ptr>

#include <SDL3/SDL_events.h>

#include "events.hpp"

namespace osgViewer
{
    class Viewer;
}

namespace SDLUtil
{
    /// \brief A wrapper around SDL's event queue, mostly used for handling input-related events.
    class InputWrapper
    {
    public:
        InputWrapper(SDL_Window* window, osg::ref_ptr<osgViewer::Viewer> viewer, bool grab);
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

        /// Moves the pointer to a position in window pixels.
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
        osg::ref_ptr<osgViewer::Viewer> mViewer;

        MouseListener* mMouseListener;
        SensorListener* mSensorListener;
        KeyListener* mKeyboardListener;
        WindowListener* mWindowListener;
        ControllerListener* mConListener;

        float mWarpX;
        float mWarpY;
        bool mWarpCompensate;
        bool mWrapPointer;

        /// False on Wayland, where SDL cannot tell whether the compositor lets it warp the pointer.
        bool mWarpMovesPointer;

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

        /// What the window listener was last told.
        bool mWindowShown;

        /// The window's size in points at its last pixel size change.
        int mWindowWidth;
        int mWindowHeight;

        float mPixelDensity;
    };

}

#endif
