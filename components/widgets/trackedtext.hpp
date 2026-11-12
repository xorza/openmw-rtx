#pragma once

#include <string_view>

#include <MyGUI_EditText.h>
#include <MyGUI_SimpleText.h>

#include "fontusers.hpp"

namespace Gui
{
    /// A text sub-widget its registry knows, `Text` being MyGUI's `EditText` or `SimpleText`.
    ///
    /// **Its size never reads a font's raster.** A text with a height keeps its size in layout units
    /// whatever the font was rasterized at; one with none takes the size its registry's fonts are set at
    /// (`FontUsers::getTextHeight`), where MyGUI's would grow with the raster.
    template <class Text>
    class Tracked : public Text, public FontUsers::Entry
    {
    public:
        explicit Tracked(FontUsers& users)
            : FontUsers::Entry(users)
        {
        }

        void setFontName(std::string_view name) override;
        void setFontHeight(int height) override;

        /// Takes its font again, and its registry's text height where it has none of its own, and
        /// has a widget sized to its text size itself again.
        void refit() override;

    private:
        void keepInterfaceHeight();
    };

    class TrackedText final : public Tracked<MyGUI::EditText>
    {
        MYGUI_RTTI_DERIVED(TrackedText)

    public:
        using Tracked::Tracked;
    };

    class TrackedSimpleText final : public Tracked<MyGUI::SimpleText>
    {
        MYGUI_RTTI_DERIVED(TrackedSimpleText)

    public:
        using Tracked::Tracked;
    };
}
