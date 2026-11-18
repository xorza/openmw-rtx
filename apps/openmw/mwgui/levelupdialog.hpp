#pragma once

#include <cstddef>
#include <cstdint>

#include <components/esm/refid.hpp>

#include "windowbase.hpp"

namespace MWGui
{

    class LevelupDialog : public WindowBase
    {
    public:
        LevelupDialog();

        void onOpen() override;

        std::string_view getWindowIdForLua() const override { return "LevelUpDialog"; }

    private:
        struct Widgets
        {
            MyGUI::Button* mButton;
            MyGUI::TextBox* mValue;
            MyGUI::TextBox* mMultiplier;
        };
        MyGUI::Button* mOkButton{};
        MyGUI::ImageBox* mClassImage{};
        MyGUI::TextBox* mLevelText{};
        MyGUI::EditBox* mLevelDescription{};

        MyGUI::Widget* mCoinBox{};
        MyGUI::ScrollView* mAssignWidget{};

        std::map<ESM::RefId, Widgets> mAttributeWidgets;
        std::vector<MyGUI::ImageBox*> mCoins;

        std::vector<ESM::RefId> mSpentAttributes;

        std::size_t mPerCol;
        std::uint32_t mCoinCount;

        void onOkButtonClicked(MyGUI::Widget* sender);
        void onAttributeClicked(MyGUI::Widget* sender);

        void assignCoins();
        void resetCoins();

        void setAttributeValues();

        std::string_view getLevelupClassImage(
            const std::int32_t combatIncreases, const std::int32_t magicIncreases, const std::int32_t stealthIncreases);

        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
        std::vector<MyGUI::Button*> mAttributeButtons;
        std::size_t mControllerFocus = 0;
    };

}
