#pragma once

#include "widget.hpp"

namespace LuaUi
{
    class LuaContainer : public WidgetExtension, public MyGUI::Widget
    {
        MYGUI_RTTI_DERIVED(LuaContainer)

    public:
        MyGUI::IntSize calculateSize() const override;
        void updateCoord() override;

    protected:
        void updateChildren() override;
        MyGUI::IntSize childScalingSize() const override;
        MyGUI::IntSize templateScalingSize() const override;

    private:
        void updateSizeToFit();
        MyGUI::IntSize mInnerSize;
        MyGUI::IntSize mOuterSize;
    };
}
