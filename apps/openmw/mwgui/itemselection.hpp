#pragma once

#include <cstdint>

#include <MyGUI_Delegate.h>

#include "windowbase.hpp"

namespace MWWorld
{
    class Ptr;
}

namespace MWGui
{
    class ItemView;
    class SortFilterItemModel;

    class ItemSelectionDialog : public WindowModal
    {
    public:
        explicit ItemSelectionDialog(const std::string& label);

        bool exit() override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_Void;
        typedef MyGUI::delegates::MultiDelegate<MWWorld::Ptr> EventHandle_Item;

        EventHandle_Item eventItemSelected;
        EventHandle_Void eventDialogCanceled;

        void openContainer(const MWWorld::Ptr& container);
        void setCategory(std::int32_t category);
        void setFilter(std::int32_t filter);

        SortFilterItemModel* getSortModel() { return mSortModel; }

    private:
        ItemView* mItemView{};
        SortFilterItemModel* mSortModel;

        void onSelectedItem(std::int32_t index);

        void onCancelButtonClicked(MyGUI::Widget* sender);
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
    };

}
