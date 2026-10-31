#include "trackedtext.hpp"

#include <MyGUI_IFont.h>

#include <components/settings/values.hpp>

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
    void Tracked<Text>::rebind()
    {
        // The font object stays through a rasterization, so its own name is the one to ask by.
        if (this->mFont != nullptr)
            setFontName(this->mFont->getResourceName());
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
