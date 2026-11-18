#include "levelupdialog.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_UString.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/soundmanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwsound/constants.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/fallback/fallback.hpp>
#include <components/settings/values.hpp>
#include <components/widgets/box.hpp>

#include "class.hpp"

namespace
{
    constexpr std::uint32_t sMaxCoins = 3;
    constexpr std::int32_t sColumnOffsets[] = { 32, 218 };
}
namespace MWGui
{
    LevelupDialog::LevelupDialog()
        : WindowBase("openmw_levelup_dialog.layout")
        , mCoinCount(sMaxCoins)
    {
        getWidget(mOkButton, "OkButton");
        getWidget(mClassImage, "ClassImage");
        getWidget(mLevelText, "LevelText");
        getWidget(mLevelDescription, "LevelDescription");
        getWidget(mCoinBox, "Coins");
        getWidget(mAssignWidget, "AssignWidget");

        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &LevelupDialog::onOkButtonClicked);

        {
            const auto& store = MWBase::Environment::get().getESMStore()->get<ESM::Attribute>();
            mPerCol = static_cast<std::size_t>(
                std::ceil(static_cast<float>(store.getSize()) / static_cast<float>(std::size(sColumnOffsets))));
            std::size_t i = 0;
            for (const ESM::Attribute& attribute : store)
            {
                const std::int32_t offset = sColumnOffsets[i / mPerCol];
                const auto row = static_cast<std::int32_t>(i % mPerCol);
                Widgets widgets{};
                widgets.mMultiplier = mAssignWidget->createWidget<MyGUI::TextBox>(
                    "SandTextVCenter", { offset, 20 * row, 100, 20 }, MyGUI::Align::Default);
                auto* hbox = mAssignWidget->createWidget<Gui::HBox>(
                    {}, { offset + 20, 20 * row, 200, 20 }, MyGUI::Align::Default);
                widgets.mButton = hbox->createWidget<Gui::AutoSizedButton>("SandTextButton", {}, MyGUI::Align::Default);
                widgets.mButton->setUserData(attribute.mId);
                widgets.mButton->eventMouseButtonClick += MyGUI::newDelegate(this, &LevelupDialog::onAttributeClicked);
                widgets.mButton->setUserString("TextPadding", "0 0");
                widgets.mButton->setUserString("ToolTipType", "Layout");
                widgets.mButton->setUserString("ToolTipLayout", "AttributeToolTip");
                widgets.mButton->setUserString("Caption_AttributeName", attribute.mName);
                widgets.mButton->setUserString("Caption_AttributeDescription", attribute.mDescription);
                widgets.mButton->setUserString("ImageTexture_AttributeImage", attribute.mIcon.getNormalized());
                widgets.mButton->setCaption(attribute.mName);
                widgets.mValue = hbox->createWidget<Gui::AutoSizedTextBox>("SandText", {}, MyGUI::Align::Default);
                mAttributeWidgets.emplace(attribute.mId, widgets);
                mAttributeButtons.emplace_back(widgets.mButton);
                ++i;
            }

            mAssignWidget->setVisibleVScroll(false);
            mAssignWidget->setCanvasSize(MyGUI::IntSize(mAssignWidget->getWidth(),
                std::max(mAssignWidget->getHeight(), static_cast<std::int32_t>(20 * mPerCol))));
            mAssignWidget->setVisibleVScroll(true);
            mAssignWidget->setViewOffset(MyGUI::IntPoint());
        }

        for (std::uint32_t i = 0; i < sMaxCoins; ++i)
        {
            auto* image = mCoinBox->createWidget<MyGUI::ImageBox>(
                "ImageBox", MyGUI::IntCoord(0, 0, 16, 16), MyGUI::Align::Default);
            image->setImageTexture("icons\\tx_goldicon.dds");
            mCoins.push_back(image);
        }

        if (Settings::gui().mControllerMenus)
        {
            mDisableGamepadCursor = true;
            mControllerButtons.mA = "#{Interface:Select}";
            mControllerButtons.mX = "#{Interface:Done}";
            mOkButton->setCaption(
                MyGUI::UString(MWBase::Environment::get().getWindowManager()->getGameSettingString("sDone", {})));
        }

        center();
    }

    void LevelupDialog::setAttributeValues()
    {
        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        MWMechanics::CreatureStats& creatureStats = player.getClass().getCreatureStats(player);
        MWMechanics::NpcStats& pcStats = player.getClass().getNpcStats(player);

        for (const ESM::Attribute& attribute : MWBase::Environment::get().getESMStore()->get<ESM::Attribute>())
        {
            auto val = static_cast<std::int32_t>(creatureStats.getAttribute(attribute.mId).getBase());
            if (std::find(mSpentAttributes.begin(), mSpentAttributes.end(), attribute.mId) != mSpentAttributes.end())
            {
                val += pcStats.getLevelupAttributeMultiplier(attribute.mId);
            }

            val = std::min(val, 100);

            mAttributeWidgets[attribute.mId].mValue->setCaption(MyGUI::utility::toString(val));
        }
    }

    void LevelupDialog::resetCoins()
    {
        constexpr std::int32_t coinSpacing = 33;
        auto curX = static_cast<std::int32_t>(static_cast<std::uint32_t>(mCoinBox->getWidth() / 2)
            - (coinSpacing * (mCoinCount - 1) + 16 * mCoinCount) / 2);
        for (std::uint32_t i = 0; i < sMaxCoins; ++i)
        {
            MyGUI::ImageBox* image = mCoins[i];
            image->detachFromWidget();
            image->attachToWidget(mCoinBox);
            if (i < mCoinCount)
            {
                mCoins[i]->setVisible(true);
                image->setCoord(MyGUI::IntCoord(curX, 0, 16, 16));
                curX += 16 + coinSpacing;
            }
            else
                mCoins[i]->setVisible(false);
        }
    }

    void LevelupDialog::assignCoins()
    {
        resetCoins();
        for (std::size_t i = 0; i < mSpentAttributes.size(); ++i)
        {
            MyGUI::ImageBox* image = mCoins[i];
            image->detachFromWidget();
            image->attachToWidget(mAssignWidget);

            const auto& attribute = mSpentAttributes[i];
            const auto& widgets = mAttributeWidgets[attribute];

            const std::int32_t xdiff = widgets.mMultiplier->getCaption().empty() ? 0 : 20;
            const auto* hbox = widgets.mButton->getParent();

            MyGUI::IntPoint pos = hbox->getPosition();
            pos.left -= 22 + xdiff;
            pos.top += (hbox->getHeight() - image->getHeight()) / 2;
            image->setPosition(pos);
        }

        setAttributeValues();
    }

    void LevelupDialog::onOpen()
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWWorld::Ptr player = world->getPlayerPtr();
        const MWMechanics::CreatureStats& creatureStats = player.getClass().getCreatureStats(player);
        const MWMechanics::NpcStats& pcStats = player.getClass().getNpcStats(player);

        setClassImage(mClassImage,
            ESM::RefId::stringRefId(
                getLevelupClassImage(pcStats.getSkillIncreasesForSpecialization(ESM::Class::Specialization::Combat),
                    pcStats.getSkillIncreasesForSpecialization(ESM::Class::Specialization::Magic),
                    pcStats.getSkillIncreasesForSpecialization(ESM::Class::Specialization::Stealth))));

        std::int32_t level = creatureStats.getLevel() + 1;
        mLevelText->setCaptionWithReplacing("#{sLevelUpMenu1} " + MyGUI::utility::toString(level));

        std::string_view levelupdescription;
        levelupdescription = Fallback::Map::getString("Level_Up_Level" + MyGUI::utility::toString(level));

        if (levelupdescription.empty())
            levelupdescription = Fallback::Map::getString("Level_Up_Default");

        mLevelDescription->setCaption(MyGUI::UString(levelupdescription));

        std::uint32_t availableAttributes = 0;
        for (const ESM::Attribute& attribute : MWBase::Environment::get().getESMStore()->get<ESM::Attribute>())
        {
            const auto& widgets = mAttributeWidgets[attribute.mId];
            if (pcStats.getAttribute(attribute.mId).getBase() < 100)
            {
                widgets.mButton->setEnabled(true);
                widgets.mValue->setEnabled(true);
                availableAttributes++;

                std::int32_t mult = pcStats.getLevelupAttributeMultiplier(attribute.mId);
                mult = std::min(mult, static_cast<std::int32_t>(100 - pcStats.getAttribute(attribute.mId).getBase()));
                if (mult <= 1)
                    widgets.mMultiplier->setCaption({});
                else
                    widgets.mMultiplier->setCaption("x" + MyGUI::utility::toString(mult));
            }
            else
            {
                widgets.mButton->setEnabled(false);
                widgets.mValue->setEnabled(false);

                widgets.mMultiplier->setCaption({});
            }
        }

        mCoinCount = std::min(sMaxCoins, availableAttributes);

        mSpentAttributes.clear();
        resetCoins();

        setAttributeValues();

        center();

        if (Settings::gui().mControllerMenus)
        {
            mControllerFocus = 0;
            for (std::size_t i = 0; i < mAttributeButtons.size(); i++)
                mAttributeButtons[i]->setStateSelected(i == 0);
        }

        // Play LevelUp Music
        MWBase::Environment::get().getSoundManager()->streamMusic(MWSound::triumphMusic, MWSound::MusicType::Normal);
    }

    void LevelupDialog::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::NpcStats& pcStats = player.getClass().getNpcStats(player);

        if (mSpentAttributes.size() < mCoinCount)
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage36}");
        else
        {
            // increase attributes
            for (std::uint32_t i = 0; i < mCoinCount; ++i)
            {
                MWMechanics::AttributeValue attribute = pcStats.getAttribute(mSpentAttributes[i]);
                attribute.setBase(attribute.getBase()
                    + static_cast<float>(pcStats.getLevelupAttributeMultiplier(mSpentAttributes[i])));

                if (attribute.getBase() >= 100)
                    attribute.setBase(100);
                pcStats.setAttribute(mSpentAttributes[i], attribute);
            }

            pcStats.levelUp();

            MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Levelup);
        }
    }

    void LevelupDialog::onAttributeClicked(MyGUI::Widget* sender)
    {
        auto attribute = *sender->getUserData<ESM::RefId>();

        auto found = std::find(mSpentAttributes.begin(), mSpentAttributes.end(), attribute);
        if (found != mSpentAttributes.end())
            mSpentAttributes.erase(found);
        else
        {
            if (mSpentAttributes.size() == mCoinCount)
                mSpentAttributes[mCoinCount - 1] = attribute;
            else
                mSpentAttributes.push_back(attribute);
        }
        assignCoins();
    }

    std::string_view LevelupDialog::getLevelupClassImage(
        const std::int32_t combatIncreases, const std::int32_t magicIncreases, const std::int32_t stealthIncreases)
    {
        std::string_view ret = "acrobat";

        std::int32_t total = combatIncreases + magicIncreases + stealthIncreases;
        if (total == 0)
            return ret;

        auto combatFraction
            = static_cast<std::int32_t>(static_cast<float>(combatIncreases) / static_cast<float>(total) * 10.f);
        auto magicFraction
            = static_cast<std::int32_t>(static_cast<float>(magicIncreases) / static_cast<float>(total) * 10.f);
        auto stealthFraction
            = static_cast<std::int32_t>(static_cast<float>(stealthIncreases) / static_cast<float>(total) * 10.f);

        if (combatFraction > 7)
            ret = "warrior";
        else if (magicFraction > 7)
            ret = "mage";
        else if (stealthFraction > 7)
            ret = "thief";

        switch (combatFraction)
        {
            case 7:
                ret = "warrior";
                break;
            case 6:
                if (stealthFraction == 1)
                    ret = "barbarian";
                else if (stealthFraction == 3)
                    ret = "crusader";
                else
                    ret = "knight";
                break;
            case 5:
                if (stealthFraction == 3)
                    ret = "scout";
                else
                    ret = "archer";
                break;
            case 4:
                ret = "rogue";
                break;
            default:
                break;
        }

        switch (magicFraction)
        {
            case 7:
                ret = "mage";
                break;
            case 6:
                if (combatFraction == 2)
                    ret = "sorcerer";
                else if (combatIncreases == 3)
                    ret = "healer";
                else
                    ret = "battlemage";
                break;
            case 5:
                ret = "witchhunter";
                break;
            case 4:
                ret = "spellsword";
                // In vanilla there's also code for "nightblade", however it seems to be unreachable.
                break;
            default:
                break;
        }

        switch (stealthFraction)
        {
            case 7:
                ret = "thief";
                break;
            case 6:
                if (magicFraction == 1)
                    ret = "agent";
                else if (magicIncreases == 3)
                    ret = "assassin";
                else
                    ret = "acrobat";
                break;
            case 5:
                if (magicIncreases == 3)
                    ret = "monk";
                else
                    ret = "pilgrim";
                break;
            case 3:
                if (magicFraction == 3)
                    ret = "bard";
                break;
            default:
                break;
        }

        return ret;
    }

    bool LevelupDialog::onControllerButtonEvent(const SDL_GamepadButtonEvent& arg)
    {
        if (arg.button == SDL_GAMEPAD_BUTTON_SOUTH)
        {
            if (mControllerFocus < mAttributeButtons.size())
                onAttributeClicked(mAttributeButtons[mControllerFocus]);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Item Gold Up"));
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_WEST)
        {
            onOkButtonClicked(mOkButton);
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
        {
            setControllerFocus(mAttributeButtons, mControllerFocus, false);
            if (mControllerFocus % mPerCol == 0)
                mControllerFocus += mPerCol - 1;
            else
                mControllerFocus--;
            setControllerFocus(mAttributeButtons, mControllerFocus, true);
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
        {
            setControllerFocus(mAttributeButtons, mControllerFocus, false);
            if (mControllerFocus % mPerCol == mPerCol - 1)
                mControllerFocus -= mPerCol - 1;
            else
                mControllerFocus++;
            setControllerFocus(mAttributeButtons, mControllerFocus, true);
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT || arg.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
        {
            setControllerFocus(mAttributeButtons, mControllerFocus, false);
            mControllerFocus = (mControllerFocus + mPerCol) % mAttributeButtons.size();
            setControllerFocus(mAttributeButtons, mControllerFocus, true);
        }

        return true;
    }
}
