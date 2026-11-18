#pragma once

#include <cstdint>

#include "windowbase.hpp"

namespace MWGui
{
    class WindowPinnableBase : public WindowBase
    {
    public:
        explicit WindowPinnableBase(const std::string& parLayout);
        bool pinned() { return mPinned; }
        void setPinned(bool pinned);
        void setPinButtonVisible(bool visible);

    private:
        void onPinButtonPressed(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);

    protected:
        virtual void onPinToggled() = 0;

        MyGUI::Widget* mPinButton;
        bool mPinned;
    };
}
