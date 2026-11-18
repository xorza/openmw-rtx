#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include <osg/Vec2i>

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/misc/notnullptr.hpp>

#include "mode.hpp"
#include "windowpinnablebase.hpp"

namespace MWRender
{
    class InventoryPreview;
}

namespace MWGui
{
    namespace Widgets
    {
        class MWDynamicStat;
    }

    class ItemView;
    class SortFilterItemModel;
    class TradeItemModel;
    class DragAndDrop;
    class ItemModel;
    class ItemTransfer;

    class InventoryWindow : public WindowPinnableBase
    {
    public:
        explicit InventoryWindow(DragAndDrop& dragAndDrop, ItemTransfer& itemTransfer);
        ~InventoryWindow() override;

        void onOpen() override;

        void onClose() override;

        /// start trading, disables item drag&drop
        void setTrading(bool trading);

        void onFrame(float dt) override;

        void pickUpObject(MWWorld::Ptr object);

        MWWorld::Ptr getAvatarSelectedItem(std::int32_t x, std::int32_t y);

        void rebuildAvatar();

        SortFilterItemModel* getSortFilterModel();
        TradeItemModel* getTradeModel();
        ItemModel* getModel();

        void updateItemView();

        void updatePlayer();

        void clear() override;

        void useItem(const MWWorld::Ptr& ptr, bool force = false);

        void setGuiMode(GuiMode mode);

        /// Placed again where its mode's place falls in the view the interface is now laid out in,
        /// and its preview sized to the frame's pixels the image now covers: both move with the
        /// view and the scale, and only a drag of the window told it of either.
        void onResChange(std::int32_t width, std::int32_t height) override;

        void refitText() override;

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        /// Cycle to previous/next weapon
        void cycle(bool next);

        std::string_view getWindowIdForLua() const override { return "Inventory"; }

        ControllerButtons* getControllerButtons() override;

    protected:
        void onTitleDoubleClicked() override;
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

    private:
        Misc::NotNullPtr<DragAndDrop> mDragAndDrop;
        Misc::NotNullPtr<ItemTransfer> mItemTransfer;

        std::int32_t mSelectedItem;

        MWWorld::Ptr mPtr;

        MWGui::ItemView* mItemView{};
        SortFilterItemModel* mSortModel;
        TradeItemModel* mTradeModel;

        MyGUI::Widget* mAvatar{};
        MyGUI::ImageBox* mAvatarImage{};
        MyGUI::TextBox* mArmorRating{};
        Widgets::MWDynamicStat* mEncumbranceBar{};

        MyGUI::Widget* mLeftPane{};
        MyGUI::Widget* mRightPane{};

        MyGUI::Button* mFilterAll{};
        MyGUI::Button* mFilterWeapon{};
        MyGUI::Button* mFilterApparel{};
        MyGUI::Button* mFilterMagic{};
        MyGUI::Button* mFilterMisc{};

        MyGUI::EditBox* mFilterEdit{};

        GuiMode mGuiMode;

        std::int32_t mLastXSize;
        std::int32_t mLastYSize;

        std::unique_ptr<MWRender::InventoryPreview> mPreview;

        bool mTrading;
        bool mUpdateNextFrame;

        void toggleMaximized();

        void onItemSelected(std::int32_t index);
        void onItemSelectedFromSourceModel(std::int32_t index);

        void onBackgroundSelected();

        enum class ControllerAction
        {
            None,
            Use,
            Transfer,
            Sell,
            Drop,
        };
        ControllerAction mPendingControllerAction;

        void sellItem(MyGUI::Widget* sender, std::size_t count);
        void dragItem(MyGUI::Widget* sender, std::size_t count);
        void transferItem(MyGUI::Widget* sender, std::size_t count);
        void dropItem(MyGUI::Widget* sender, std::size_t count);
        void equipItem(std::size_t count);

        void onWindowResize(MyGUI::Window* sender);
        void onFilterChanged(MyGUI::Widget* sender);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onAvatarClicked(MyGUI::Widget* sender);
        void onPinToggled() override;

        void updateEncumbranceBar();
        void notifyContentChanged();
        void dirtyPreview();
        void updatePreviewSize();

        /// Places the window at its mode's place in the view, and answers whether its size moved.
        bool placeForMode();
        void updateArmorRating();

        MyGUI::IntSize getPreviewViewportSize() const;
        osg::Vec2i mapPreviewWindowToViewport(std::int32_t x, std::int32_t y) const;

        void adjustPanes();

        /// Unequips count items from mSelectedItem, if it is equipped, and then updates mSelectedItem in case the items
        /// were re-stacked
        void ensureSelectedItemUnequipped(std::int32_t count);
    };
}
