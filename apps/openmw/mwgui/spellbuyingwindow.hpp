#pragma once

#include <cstddef>
#include <cstdint>

#include <components/esm/refid.hpp>

#include "referenceinterface.hpp"
#include "windowbase.hpp"
namespace ESM
{
    struct Spell;
}

namespace MyGUI
{
    class Gui;
    class Widget;
}

namespace MWGui
{
    class SpellBuyingWindow : public ReferenceInterface, public WindowBase
    {
    public:
        SpellBuyingWindow();

        void setPtr(const MWWorld::Ptr& actor) override;
        void setPtr(const MWWorld::Ptr& actor, std::int32_t startOffset);

        void onFrame(float dt) override { checkReferenceAvailable(); }
        void clear() override { resetReference(); }

        void onResChange(std::int32_t, std::int32_t) override { center(); }

        std::string_view getWindowIdForLua() const override { return "SpellBuying"; }

    protected:
        MyGUI::Button* mCancelButton{};
        MyGUI::TextBox* mPlayerGold{};

        MyGUI::ScrollView* mSpellsView{};

        std::map<MyGUI::Widget*, ESM::RefId> mSpellsWidgetMap;
        /// List of enabled/purchasable spells and their index in the full list.
        std::vector<std::pair<MyGUI::Button*, std::size_t>> mSpellButtons;

        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onSpellButtonClick(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, std::int32_t rel);
        void addSpell(const ESM::Spell& spell);
        void clearSpells();
        std::int32_t mCurrentY;

        void updateLabels();

        void onReferenceUnavailable() override;

        bool playerHasSpell(const ESM::RefId& id);

    private:
        static bool sortSpells(const ESM::Spell* left, const ESM::Spell* right);
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;
        std::size_t mControllerFocus = 0;
    };
}
