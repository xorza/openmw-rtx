#pragma once

namespace MyGUI
{
    class Widget;
}

namespace LuaUi
{
    void dispatchMouseWheel(MyGUI::Widget* widget, float left, float top, float x, float y);
}
