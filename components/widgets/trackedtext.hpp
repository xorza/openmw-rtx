#pragma once

#include <string_view>

#include <MyGUI_EditText.h>
#include <MyGUI_SimpleText.h>

#include "textregistry.hpp"

namespace Gui
{
    /// A text sub-widget its registry knows, `Text` being MyGUI's `EditText` or `SimpleText`.
    ///
    /// **Its size never reads a font's raster.** A glyph is drawn at the text's height over the
    /// font's own, so a text with a height keeps its size in layout units whatever the font was
    /// rasterized at; a text with none of its own takes the interface's font size, `[GUI] font
    /// size`, where MyGUI's would take the font's height in raster pixels and grow with the raster.
    template <class Text>
    class Tracked : public Text, public TextRegistry::Entry
    {
    public:
        explicit Tracked(TextRegistry& registry)
            : TextRegistry::Entry(registry)
        {
        }

        void setFontName(std::string_view name) override;
        void setFontHeight(int height) override;

        /// Takes its font again, and has a widget sized to its text size itself again.
        void rebind() override;

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
