#pragma once

#include <cstddef>
#include <cstdint>

#include "referenceinterface.hpp"
#include "windowbase.hpp"

namespace Gui
{
    class NumericEditBox;
}

namespace MyGUI
{
    class ControllerItem;
}

namespace MWGui
{
    class ItemView;
    class SortFilterItemModel;
    class TradeItemModel;

    class TradeWindow : public WindowBase, public ReferenceInterface
    {
    public:
        TradeWindow();

        void setPtr(const MWWorld::Ptr& actor) override;

        void onClose() override;
        void onFrame(float dt) override;
        void clear() override { resetReference(); }

        bool exit() override;

        void resetReference() override;

        void onDeleteCustomData(const MWWorld::Ptr& ptr) override;

        void updateItemView();

        void onInventoryUpdate(const MWWorld::Ptr& ptr) override;

        typedef MyGUI::delegates::MultiDelegate<> EventHandle_TradeDone;
        EventHandle_TradeDone eventTradeDone;

        std::string_view getWindowIdForLua() const override { return "Trade"; }

        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
        void setActiveControllerWindow(bool active) override;

    private:
        friend class InventoryWindow;

        ItemView* mItemView{};
        SortFilterItemModel* mSortModel;
        TradeItemModel* mTradeModel;

        static const float sBalanceChangeInitialPause; // in seconds
        static const float sBalanceChangeInterval; // in seconds

        MyGUI::Button* mFilterAll{};
        MyGUI::Button* mFilterWeapon{};
        MyGUI::Button* mFilterApparel{};
        MyGUI::Button* mFilterMagic{};
        MyGUI::Button* mFilterMisc{};

        MyGUI::EditBox* mFilterEdit{};

        MyGUI::Button* mIncreaseButton{};
        MyGUI::Button* mDecreaseButton{};
        MyGUI::TextBox* mTotalBalanceLabel{};
        Gui::NumericEditBox* mTotalBalance{};

        MyGUI::Widget* mBottomPane{};

        MyGUI::Button* mMaxSaleButton{};
        MyGUI::Button* mCancelButton{};
        MyGUI::Button* mOfferButton{};
        MyGUI::TextBox* mPlayerGold{};
        MyGUI::TextBox* mMerchantGold{};

        std::int32_t mItemToSell;

        std::int32_t mCurrentBalance;
        std::int32_t mCurrentMerchantOffer;

        bool mUpdateNextFrame;

        void updateOffer();

        void onItemSelected(std::int32_t index);
        void sellItem(MyGUI::Widget* sender, std::size_t count);

        void borrowItem(std::int32_t index, std::size_t count);
        void returnItem(std::int32_t index, std::size_t count);

        std::int32_t getMerchantServices();

        void onFilterChanged(MyGUI::Widget* sender);
        void onNameFilterChanged(MyGUI::EditBox* sender);
        void onOfferButtonClicked(MyGUI::Widget* sender);
        void onAccept(MyGUI::EditBox* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onMaxSaleButtonClicked(MyGUI::Widget* sender);
        void onIncreaseButtonPressed(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);
        void onDecreaseButtonPressed(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);
        void onBalanceButtonReleased(MyGUI::Widget* sender, std::int32_t left, std::int32_t top, MyGUI::MouseButton id);
        void onBalanceValueChanged(std::int32_t value);
        void onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller);
        void onOfferSubmitted(MyGUI::Widget* sender, std::size_t offerAmount);

        void addRepeatController(MyGUI::Widget* widget);

        void onIncreaseButtonTriggered();
        void onDecreaseButtonTriggered();

        void addOrRemoveGold(std::int32_t amount, const MWWorld::Ptr& actor);

        void updateLabels();

        void onReferenceUnavailable() override;

        std::int32_t getMerchantGold();
    };
}
