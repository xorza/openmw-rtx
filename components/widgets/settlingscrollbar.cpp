#include "settlingscrollbar.hpp"

#include <array>
#include <cstdint>

#include <MyGUI_Button.h>

namespace Gui
{
    void SettlingScrollBar::initialiseOverride()
    {
        ScrollBar::initialiseOverride();

        // After MyGUI's own handlers, which the base subscribed first: the position is moved by then.
        const std::array<MyGUI::Widget*, 5> parts{ mWidgetStart, mWidgetEnd, mWidgetTrack, mWidgetFirstPart,
            mWidgetSecondPart };
        for (MyGUI::Widget* const part : parts)
            if (part != nullptr)
                part->eventMouseButtonReleased += MyGUI::newDelegate(this, &SettlingScrollBar::notifyReleased);

        if (mWidgetTrack != nullptr)
            mWidgetTrack->eventMouseWheel += MyGUI::newDelegate(this, &SettlingScrollBar::notifyWheel);
    }

    void SettlingScrollBar::onMouseWheel(std::int32_t rel)
    {
        ScrollBar::onMouseWheel(rel);
        settle();
    }

    void SettlingScrollBar::notifyReleased(MyGUI::Widget*, std::int32_t, std::int32_t, MyGUI::MouseButton)
    {
        settle();
    }

    void SettlingScrollBar::notifyWheel(MyGUI::Widget*, std::int32_t)
    {
        settle();
    }

    void SettlingScrollBar::settle()
    {
        eventScrollSettled(this, getScrollPosition());
    }
}
