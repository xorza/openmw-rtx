#include "inventorytabsoverlay.hpp"

#include <cstddef>
#include <cstdint>

#include <MyGUI_Button.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_Window.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/inputmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>

namespace MWGui
{
    InventoryTabsOverlay::InventoryTabsOverlay()
        : WindowBase("openmw_inventory_tabs.layout")
    {
        MyGUI::Button* tab = nullptr;
        constexpr std::string_view kTabIds[] = { "TabMap", "TabInventory", "TabSpells", "TabStats" };

        for (const std::string_view id : kTabIds)
        {
            getWidget(tab, id);
            tab->eventMouseButtonClick += MyGUI::newDelegate(this, &InventoryTabsOverlay::onTabClicked);
            mTabs.push_back(tab);
        }

        MyGUI::ImageBox* image = nullptr;
        getWidget(image, "BtnL2Image");
        image->setImageTexture(
            MWBase::Environment::get().getInputManager()->getControllerAxisIcon(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));

        getWidget(image, "BtnR2Image");
        image->setImageTexture(
            MWBase::Environment::get().getInputManager()->getControllerAxisIcon(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    }

    std::int32_t InventoryTabsOverlay::getHeight()
    {
        auto* window = mMainWidget->castType<MyGUI::Window>();
        return window->getHeight();
    }

    void InventoryTabsOverlay::onTabClicked(MyGUI::Widget* sender)
    {
        if (!MWBase::Environment::get().getWindowManager()->getJournalAllowed())
            return;

        for (std::size_t i = 0; i < mTabs.size(); ++i)
        {
            if (mTabs[i] == sender)
            {
                MWBase::Environment::get().getWindowManager()->setActiveControllerWindow(GM_Inventory, i);
                setTab(i);
                break;
            }
        }
    }

    void InventoryTabsOverlay::setTab(std::size_t index)
    {
        for (std::size_t i = 0; i < mTabs.size(); ++i)
            mTabs[i]->setStateSelected(i == index);
    }
}
