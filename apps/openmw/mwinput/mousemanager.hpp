#ifndef MWINPUT_MWMOUSEMANAGER_H
#define MWINPUT_MWMOUSEMANAGER_H

#include <components/sdlutil/events.hpp>
#include <components/settings/settings.hpp>

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

        void mouseMoved(const SDLUtil::MouseMotionEvent& arg) override;
        void mousePressed(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseReleased(const SDL_MouseButtonEvent& arg, Uint8 id) override;
        void mouseWheelMoved(const SDL_MouseWheelEvent& arg) override;

        bool injectMouseButtonPress(Uint8 button);
        bool injectMouseButtonRelease(Uint8 button);
        void injectMouseMove(float xMove, float yMove, float mouseWheelMove);
        void warpMouse();
        void warpMouseToWidget(MyGUI::Widget* widget);

        void setMouseLookEnabled(bool enabled) { mMouseLookEnabled = enabled; }
        void setGuiCursorEnabled(bool enabled) { mGuiCursorEnabled = enabled; }

        float getMouseMoveX() const { return mMouseMoveX; }
        float getMouseMoveY() const { return mMouseMoveY; }

    private:
        BindingsManager* mBindingsManager;
        SDLUtil::InputWrapper* mInputWrapper;
        const MWRender::Renderer& mRenderer;

        float mGuiCursorX;
        float mGuiCursorY;
        int mMouseWheel;
        bool mMouseLookEnabled;
        bool mGuiCursorEnabled;
        float mLastWarpX;
        float mLastWarpY;

        float mMouseMoveX;
        float mMouseMoveY;
    };
}
#endif
