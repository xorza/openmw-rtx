#pragma once

#include <cstddef>

#include <MyGUI_ScrollBar.h>

namespace Gui
{
    /// A scroll bar that also says where the player left it: on the release of a drag of its thumb,
    /// of a click on a button or on the track, and on each step of the wheel. For a setting whose
    /// change moves the window the bar stands in — the interface's scale — which applied at every
    /// position of a drag would move the bar under the pointer and feed the drag its own movement.
    class SettlingScrollBar final : public MyGUI::ScrollBar
    {
        MYGUI_RTTI_DERIVED(SettlingScrollBar)

    public:
        /// The position the bar was left at.
        MyGUI::EventHandle_ScrollBarPtrSizeT eventScrollSettled;

    protected:
        void initialiseOverride() override;
        void onMouseWheel(int rel) override;

    private:
        void notifyReleased(MyGUI::Widget* sender, int left, int top, MyGUI::MouseButton id);
        void notifyWheel(MyGUI::Widget* sender, int rel);

        void settle();
    };
}
