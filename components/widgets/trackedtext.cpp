#include "trackedtext.hpp"

#include <MyGUI_IFont.h>
#include <MyGUI_Widget.h>

#include <components/settings/values.hpp>

#include "box.hpp"

namespace Gui
{
    template <class Text>
    void Tracked<Text>::setFontName(std::string_view name)
    {
        Text::setFontName(name);
        keepInterfaceHeight();
    }

    template <class Text>
    void Tracked<Text>::setFontHeight(int height)
    {
        Text::setFontHeight(height);
        keepInterfaceHeight();
    }

    template <class Text>
    void Tracked<Text>::refit()
    {
        // The font object stays through a rasterization, so its own name is the one to ask by.
        if (this->mFont == nullptr)
            return;
        setFontName(this->mFont->getResourceName());

        // Hinting at another raster moves an advance by a pixel, and a widget sized to its text
        // follows it.
        if (auto* const widget = dynamic_cast<MyGUI::Widget*>(this->mCroppedParent))
            if (auto* const sized = dynamic_cast<AutoSizedWidget*>(widget))
                sized->remeasure(*widget);
    }

    template <class Text>
    void Tracked<Text>::keepInterfaceHeight()
    {
        if (this->mCustomFontHeight == 0)
            this->mFontHeight = Settings::gui().mFontSize;
    }

    template class Tracked<MyGUI::EditText>;
    template class Tracked<MyGUI::SimpleText>;
}
