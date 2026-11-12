#pragma once

#include <MyGUI_ListBox.h>

#include "fontusers.hpp"

namespace Gui
{
    /// MyGUI's list, standing in MyGUI's `ListBox` factory under its name
    /// (`FontUsers::Users::Lists`), so every list a layout, a skin or a combo box makes is
    /// one. **Its lines stand a line of the interface's text high**, its registry's text height and
    /// a pixel above and below, and follow a fit to another font size; a list whose skin states its
    /// own `HeightLine` keeps it.
    ///
    /// MyGUI 3.4.3 fixes a list's line height as the list is made. MyGUI's own `ListBox::setItemHeight`
    /// (commit 3c5c50c, after 3.4.3) changes it on a made list; this is that function, ported to the
    /// private members 3.4.3 holds it in. A combo box's open list is not sized again, since the font size
    /// changes from the settings window alone, whose press closes every open list first.
    class ListBox final : public MyGUI::ListBox, public FontUsers::Entry
    {
        MYGUI_RTTI_DERIVED(ListBox)

    public:
        explicit ListBox(FontUsers& users)
            : FontUsers::Entry(users)
        {
        }

        /// Takes its registry's line height where its skin states none.
        void refit() override;

        /// Stands every line at `height`, at least a pixel, keeping the scroll's position in
        /// pixels where the new range holds it, as MyGUI's own does.
        void setItemHeight(int height);

        int getItemHeight() const;

    protected:
        void initialiseOverride() override;

    private:
        bool mHeightStated = false;
    };
}
