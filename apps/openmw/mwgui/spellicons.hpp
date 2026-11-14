#pragma once

#include <map>

#include <components/esm/refid.hpp>

namespace MyGUI
{
    class Widget;
    class ImageBox;
}

namespace MWGui
{

    class SpellIcons
    {
    public:
        void updateWidgets(MyGUI::Widget* parent, bool adjustSize);

    private:
        std::map<ESM::RefId, MyGUI::ImageBox*> mWidgetMap;
    };

}
