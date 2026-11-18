#pragma once

#include <cstdint>
#include <vector>

#include "text.hpp"
#include "widget.hpp"

namespace LuaUi
{
    class LuaWindow : public MyGUI::Widget, public WidgetExtension
    {
        MYGUI_RTTI_DERIVED(LuaWindow)

    public:
        LuaWindow();
        void updateTemplate() override;
        void updateProperties() override;

    private:
        LuaText* mCaption;
        std::map<MyGUI::Widget*, WidgetExtension*> mActionWidgets;
        MyGUI::IntPoint mPreviousMouse;
        MyGUI::IntCoord mChangeScale;

        MyGUI::IntCoord mMoveResize;

        const std::vector<std::string_view>& allUsedProperties() const override;

    protected:
        void notifyMousePress(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);
        void notifyMouseDrag(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);
    };
}
