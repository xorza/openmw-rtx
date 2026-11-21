#pragma once

#include <span>
#include <string>

namespace MyGUI
{
    class ComboBox;
}

namespace Gui
{
    /// **Puts `items` in `box` in place of what it lists, the item it had selected still selected** where `items` holds
    /// it: MyGUI's `removeAllItems` forgets the selection and leaves its caption, so the wheel stepped on from the
    /// first item and not from the one shown (upstream #8143). An item `items` lacks leaves none selected.
    void replaceComboBoxItems(MyGUI::ComboBox& box, std::span<const std::string> items);
}
