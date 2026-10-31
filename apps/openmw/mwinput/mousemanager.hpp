#ifndef MWINPUT_MWMOUSEMANAGER_H
#define MWINPUT_MWMOUSEMANAGER_H

#include <optional>

#include <MyGUI_Types.h>
#include <osg/Vec2f>

#include <components/sdlutil/events.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/inputmanager.hpp"

namespace SDLUtil
{
    class InputWrapper;
}

namespace MWRender
{
    class Renderer;
}

namespace MWInput
{
    class BindingsManager;

    class MouseManager : public SDLUtil::MouseListener
    {
    public:
        /// `renderer` places the frame in the window, which a point of the pointer is mapped through.
        MouseManager(
            BindingsManager* bindingsManager, SDLUtil::InputWrapper* inputWrapper, const MWRender::Renderer& renderer);

        virtual ~MouseManager() = default;

        void updateCursorMode();
        void update(float dt);

        void setCursorMode(MWBase::CursorMode mode) { mCursorMode = mode; }
        MWBase::CursorMode getCursorMode() const { return mCursorMode; }
        /// In the interface's units, as MyGUI lays out.
        void setCursorPosition(const osg::Vec2f& position);
        osg::Vec2f getCursorPosition() const { return cursorInInterface(); }

        void mouseMoved(const SDLUtil::MouseMotionEvent& arg) override;
        void mousePressed(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseWheelMoved(const SDL_MouseWheelEvent& arg) override;

        bool injectMouseButtonPress(Uint8 button);
        bool injectMouseButtonRelease(Uint8 button);

        /// By `xMove` and `yMove` in the interface's units.
        void injectMouseMove(float xMove, float yMove, float mouseWheelMove);
        void warpMouse();
        void warpMouseToWidget(MyGUI::Widget* widget);

        void setMouseLookEnabled(bool enabled) { mMouseLookEnabled = enabled; }
        void setGuiCursorEnabled(bool enabled) { mGuiCursorEnabled = enabled; }

        float getMouseMoveX() const { return mMouseMoveX; }
        float getMouseMoveY() const { return mMouseMoveY; }

    private:
        /// The cursor in the interface's units, which the scale of the moment divides it into.
        osg::Vec2f cursorInInterface() const;
        MyGUI::IntPoint cursorPoint() const;

        /// `position`, in the interface's units, held inside the view and kept in the frame's.
        void placeCursor(osg::Vec2f position);

        BindingsManager* mBindingsManager;
        SDLUtil::InputWrapper* mInputWrapper;
        const MWRender::Renderer& mRenderer;

        /// **In the frame's pixels, which no scale moves**: a cursor kept in the interface's units
        /// would point at another widget after the interface is laid out at another scale, until
        /// the mouse moved.
        osg::Vec2f mGuiCursor;
        int mMouseWheel;
        bool mMouseLookEnabled;
        bool mGuiCursorEnabled;
        MWBase::CursorMode mCursorMode;

        /// Where the last warp put the cursor, in the frame's pixels, or nothing since.
        std::optional<osg::Vec2f> mLastWarp;

        float mMouseMoveX;
        float mMouseMoveY;
    };
}
#endif
