#include "comboboxitems.hpp"

#include <cstddef>

#include <MyGUI_ComboBox.h>
#include <MyGUI_UString.h>

namespace Gui
{
    void replaceComboBoxItems(MyGUI::ComboBox& box, std::span<const std::string> items)
    {
        const std::size_t selected = box.getIndexSelected();
        const MyGUI::UString chosen = selected == MyGUI::ITEM_NONE ? MyGUI::UString() : box.getItemNameAt(selected);

        box.removeAllItems();
        for (const std::string& item : items)
            box.addItem(item);

        if (selected == MyGUI::ITEM_NONE)
            return;
        const std::size_t kept = box.findItemIndexWith(chosen);
        if (kept != MyGUI::ITEM_NONE)
            box.setIndexSelected(kept);
    }
}
