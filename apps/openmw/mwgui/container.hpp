#pragma once

#include <cstddef>
#include <cstdint>

#include <components/misc/notnullptr.hpp>

#include "itemmodel.hpp"
#include "referenceinterface.hpp"
#include "windowbase.hpp"

namespace MyGUI
{
    class Gui;
    class Widget;
}

namespace MWGui
{
    class ContainerWindow;
    class ItemView;
    class SortFilterItemModel;
    class ItemTransfer;

    class ContainerWindow : public WindowBase, public ReferenceInterface
    {
    public:
        explicit ContainerWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer);

        void setPtr(const MWWorld::Ptr& container) override;

        void onOpen() override;

        void onClose() override;

        void clear() override { resetReference(); }

        void onFrame(float dt) override;

        void resetReference() override;

        void onDeleteCustomData(const MWWorld::Ptr& ptr) override;

        void treatNextOpenAsLoot() { mTreatNextOpenAsLoot = true; }

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        std::string_view getWindowIdForLua() const override { return "Container"; }

        ControllerButtons* getControllerButtons() override;
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

        MWGui::ItemView* getItemView() { return mItemView; }
        ItemModel* getModel() { return mModel; }

    private:
        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        MWGui::ItemView* mItemView{};
        SortFilterItemModel* mSortModel;
        ItemModel* mModel;
        std::int32_t mSelectedItem;
        bool mUpdateNextFrame;
        bool mTreatNextOpenAsLoot;
        MyGUI::Button* mDisposeCorpseButton{};
        MyGUI::Button* mTakeButton{};
        MyGUI::Button* mCloseButton{};

        void onItemSelected(std::int32_t index);
        void onBackgroundSelected();
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);
        void dropItem();
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeAllButtonClicked(MyGUI::Widget* sender);
        void onDisposeCorpseButtonClicked(MyGUI::Widget* sender);

        void onReferenceUnavailable() override;
    };
}
