#pragma once

#include <cstddef>
#include <cstdint>

#include "windowbase.hpp"

namespace MyGUI
{
    class Button;
}

namespace MWGui
{
    class InventoryTabsOverlay : public WindowBase
    {
    public:
        InventoryTabsOverlay();

        std::int32_t getHeight();
        void setTab(std::size_t index);

    private:
        std::vector<MyGUI::Button*> mTabs;

        void onTabClicked(MyGUI::Widget* sender);
    };
}
