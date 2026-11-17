#include "waitdialog.hpp"

#include <cstddef>
#include <cstdint>
#include <format>

#include <MyGUI_ProgressBar.h>
#include <MyGUI_ScrollBar.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/mechanicsmanager.hpp>
#include <apps/openmw/mwbase/statemanager.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwbase/world.hpp>
#include <apps/openmw/mwmechanics/actorutil.hpp>
#include <apps/openmw/mwmechanics/creaturestats.hpp>
#include <apps/openmw/mwmechanics/npcstats.hpp>
#include <apps/openmw/mwworld/cellstore.hpp>
#include <apps/openmw/mwworld/class.hpp>
#include <apps/openmw/mwworld/datetimemanager.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/misc/rng.hpp>
#include <components/settings/values.hpp>
#include <components/widgets/box.hpp>

namespace MWGui
{

    WaitDialogProgressBar::WaitDialogProgressBar()
        : WindowBase("openmw_wait_dialog_progressbar.layout")
    {
        getWidget(mProgressBar, "ProgressBar");
        getWidget(mProgressText, "ProgressText");
    }

    void WaitDialogProgressBar::onOpen()
    {
        center();
    }

    void WaitDialogProgressBar::setProgress(std::int32_t cur, std::int32_t total)
    {
        mProgressBar->setProgressRange(static_cast<std::size_t>(total));
        mProgressBar->setProgressPosition(static_cast<std::size_t>(cur));
        mProgressText->setCaption(std::format("{}/{}", cur, total));
    }

    // ---------------------------------------------------------------------------------------------------------

    WaitDialog::WaitDialog()
        : WindowBase("openmw_wait_dialog.layout")
        , mSleeping(false)
        , mHours(1)
        , mManualHours(1)
        , mFadeTimeRemaining(0)
        , mInterruptAt(-1)
        , mProgressBar()
    {
        getWidget(mDateTimeText, "DateTimeText");
        getWidget(mRestText, "RestText");
        getWidget(mHourText, "HourText");
        getWidget(mUntilHealedButton, "UntilHealedButton");
        getWidget(mWaitButton, "WaitButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mHourSlider, "HourSlider");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &WaitDialog::onCancelButtonClicked);
        mUntilHealedButton->eventMouseButtonClick += MyGUI::newDelegate(this, &WaitDialog::onUntilHealedButtonClicked);
        mWaitButton->eventMouseButtonClick += MyGUI::newDelegate(this, &WaitDialog::onWaitButtonClicked);
        mHourSlider->eventScrollChangePosition += MyGUI::newDelegate(this, &WaitDialog::onHourSliderChangedPosition);

        mCancelButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &WaitDialog::onKeyButtonPressed);
        mWaitButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &WaitDialog::onKeyButtonPressed);
        mUntilHealedButton->eventKeyButtonPressed += MyGUI::newDelegate(this, &WaitDialog::onKeyButtonPressed);

        mTimeAdvancer.eventProgressChanged += MyGUI::newDelegate(this, &WaitDialog::onWaitingProgressChanged);
        mTimeAdvancer.eventInterrupted += MyGUI::newDelegate(this, &WaitDialog::onWaitingInterrupted);
        mTimeAdvancer.eventFinished += MyGUI::newDelegate(this, &WaitDialog::onWaitingFinished);

        mControllerButtons.mB = "#{Interface:Cancel}";
        mDisableGamepadCursor = Settings::gui().mControllerMenus;
    }

    void WaitDialog::setPtr(const MWWorld::Ptr& ptr)
    {
        const std::int32_t restFlags = MWBase::Environment::get().getWorld()->canRest();

        const bool underwater = (restFlags & MWBase::World::Rest_PlayerIsUnderwater) != 0;
        // Resting in air is allowed if you're using a bed
        const bool inAir = ptr.isEmpty() && (restFlags & MWBase::World::Rest_PlayerIsInAir) != 0;
        const bool enemiesNearby = (restFlags & MWBase::World::Rest_EnemiesAreNearby) != 0;
        const bool solidGround = !underwater && !inAir;

        if (!solidGround || enemiesNearby)
        {
            if (!solidGround)
                MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage1}");

            if (enemiesNearby)
                MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage2}");

            MWBase::Environment::get().getWindowManager()->popGuiMode();
            return;
        }

        const bool canSleep = !ptr.isEmpty() || (restFlags & MWBase::World::Rest_CanSleep) != 0;
        setCanRest(canSleep);

        if (mUntilHealedButton->getVisible())
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mUntilHealedButton);
        else
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mWaitButton);
    }

    bool WaitDialog::exit()
    {
        bool canExit = !mTimeAdvancer.isRunning(); // Only exit if not currently waiting
        if (canExit)
        {
            clear();
            stopWaiting();
        }
        return canExit;
    }

    void WaitDialog::clear()
    {
        mSleeping = false;
        mHours = 1;
        mManualHours = 1;
        mFadeTimeRemaining = 0;
        mInterruptAt = -1;
        mTimeAdvancer.stop();
    }

    void WaitDialog::onOpen()
    {
        if (mTimeAdvancer.isRunning())
        {
            mProgressBar.setVisible(true);
            setVisible(false);
            return;
        }
        else
        {
            mProgressBar.setVisible(false);
        }

        if (!MWBase::Environment::get().getWindowManager()->getRestEnabled())
        {
            MWBase::Environment::get().getWindowManager()->popGuiMode();
        }

        onHourSliderChangedPosition(mHourSlider, 0);
        mHourSlider->setScrollPosition(0);

        const MWWorld::DateTimeManager& timeManager = *MWBase::Environment::get().getWorld()->getTimeManager();
        std::string_view month = timeManager.getMonthName();
        ESM::EpochTimeStamp currentDate = timeManager.getEpochTimeStamp();
        std::string timeOfDay = timeManager.formatTimeOfDay(timeManager.getTimeStamp().getHour());
        std::string dateTimeText = std::format(
            "{} {} (#{{Calendar:day}} {}) {}", currentDate.mDay, month, timeManager.getTimeStamp().getDay(), timeOfDay);
        mDateTimeText->setCaptionWithReplacing(dateTimeText);
    }

    void WaitDialog::onUntilHealedButtonClicked(MyGUI::Widget* /*sender*/)
    {
        std::int32_t autoHours = MWBase::Environment::get().getMechanicsManager()->getHoursToRest();

        startWaiting(autoHours);
    }

    void WaitDialog::onWaitButtonClicked(MyGUI::Widget* /*sender*/)
    {
        startWaiting(mManualHours);
    }

    void WaitDialog::startWaiting(std::int32_t hoursToWait)
    {
        if (Settings::saves().mAutosave) // autosaves when enabled
            MWBase::Environment::get().getStateManager()->quickSave("Autosave");

        MWBase::World* world = MWBase::Environment::get().getWorld();
        MWBase::Environment::get().getWindowManager()->fadeScreenOut(0.2f);
        mFadeTimeRemaining = 0.4f;
        setVisible(false);

        mHours = hoursToWait;

        // FIXME: move this somewhere else?
        mInterruptAt = -1;
        MWWorld::Ptr player = world->getPlayerPtr();
        if (mSleeping && player.getCell()->isExterior())
        {
            const ESM::RefId& regionstr = player.getCell()->getCell()->getRegion();
            if (!regionstr.empty())
            {
                const ESM::Region* region = world->getStore().get<ESM::Region>().find(regionstr);
                if (!region->mSleepList.empty())
                {
                    // figure out if player will be woken while sleeping
                    std::int32_t x = Misc::Rng::rollDice(hoursToWait, world->getPrng());
                    float fSleepRandMod
                        = world->getStore().get<ESM::GameSetting>().find("fSleepRandMod")->mValue.getFloat();
                    if (static_cast<float>(x) < fSleepRandMod * static_cast<float>(hoursToWait))
                    {
                        float fSleepRestMod
                            = world->getStore().get<ESM::GameSetting>().find("fSleepRestMod")->mValue.getFloat();
                        auto interruptAtHoursRemaining
                            = static_cast<std::int32_t>(fSleepRestMod * static_cast<float>(hoursToWait));
                        if (interruptAtHoursRemaining != 0)
                        {
                            mInterruptAt = hoursToWait - interruptAtHoursRemaining;
                            mInterruptCreatureList = region->mSleepList;
                        }
                    }
                }
            }
        }

        mProgressBar.setProgress(0, hoursToWait);
    }

    void WaitDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Rest);
    }

    void WaitDialog::onHourSliderChangedPosition(MyGUI::ScrollBar* sender, std::size_t position)
    {
        mHourText->setCaptionWithReplacing(std::format("{} #{{sRestMenu2}}", position + 1));
        mManualHours = static_cast<std::int32_t>(position + 1);
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mWaitButton);
    }

    void WaitDialog::onKeyButtonPressed(MyGUI::Widget* /*sender*/, MyGUI::KeyCode key, MyGUI::Char character)
    {
        if (key == MyGUI::KeyCode::ArrowUp)
            mHourSlider->setScrollPosition(
                std::min(mHourSlider->getScrollPosition() + 1, mHourSlider->getScrollRange() - 1));
        else if (key == MyGUI::KeyCode::ArrowDown)
            mHourSlider->setScrollPosition(
                static_cast<std::size_t>(std::max(static_cast<std::int32_t>(mHourSlider->getScrollPosition()) - 1, 0)));
        else
            return;
        onHourSliderChangedPosition(mHourSlider, mHourSlider->getScrollPosition());
    }

    void WaitDialog::onWaitingProgressChanged(std::int32_t cur, std::int32_t total)
    {
        mProgressBar.setProgress(cur, total);
        MWBase::Environment::get().getMechanicsManager()->rest(1, mSleeping);
        MWBase::Environment::get().getWorld()->advanceTime(1);

        MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.getClass().getCreatureStats(player).isDead())
            stopWaiting();
    }

    void WaitDialog::onWaitingInterrupted()
    {
        MWBase::Environment::get().getWindowManager()->messageBox("#{sSleepInterrupt}");
        MWBase::Environment::get().getWorld()->spawnRandomCreature(mInterruptCreatureList);
        stopWaiting();
    }

    void WaitDialog::onWaitingFinished()
    {
        stopWaiting();

        MWWorld::Ptr player = MWMechanics::getPlayer();
        const MWMechanics::NpcStats& pcstats = player.getClass().getNpcStats(player);

        // trigger levelup if possible
        const MWWorld::Store<ESM::GameSetting>& gmst
            = MWBase::Environment::get().getESMStore()->get<ESM::GameSetting>();
        if (mSleeping && pcstats.getLevelProgress() >= gmst.find("iLevelUpTotal")->mValue.getInteger())
        {
            MWBase::Environment::get().getWindowManager()->pushGuiMode(GM_Levelup);
        }
    }

    void WaitDialog::setCanRest(bool canRest)
    {
        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
        bool full = (stats.getHealth().getCurrent() >= stats.getHealth().getModified())
            && (stats.getMagicka().getCurrent() >= stats.getMagicka().getModified());
        MWMechanics::NpcStats& npcstats = player.getClass().getNpcStats(player);
        bool werewolf = npcstats.isWerewolf();

        mUntilHealedButton->setVisible(canRest && !full);
        mWaitButton->setCaptionWithReplacing(canRest ? "#{sRest}" : "#{sWait}");
        mRestText->setCaptionWithReplacing(
            canRest ? "#{sRestMenu3}" : (werewolf ? "#{sWerewolfRestMessage}" : "#{sRestIllegal}"));

        mSleeping = canRest;

        auto* box = dynamic_cast<Gui::Box*>(mMainWidget);
        if (box == nullptr)
            throw std::runtime_error("main widget must be a box");
        box->notifyChildrenSizeChanged();
        center();
    }

    void WaitDialog::onFrame(float dt)
    {
        mTimeAdvancer.onFrame(dt);

        if (mFadeTimeRemaining <= 0)
            return;

        mFadeTimeRemaining -= dt;

        if (mFadeTimeRemaining <= 0)
        {
            mProgressBar.setVisible(true);
            mTimeAdvancer.run(mHours, mInterruptAt);
        }
    }

    ControllerButtons* WaitDialog::getControllerButtons()
    {
        mControllerButtons.mX.clear();
        if (mSleeping)
        {
            mControllerButtons.mA = "#{Interface:Rest}";
            if (mUntilHealedButton->getVisible())
                mControllerButtons.mX = "#{Interface:UntilHealed}";
        }
        else
        {
            mControllerButtons.mA = "#{Interface:Wait}";
        }
        return &mControllerButtons;
    }

    bool WaitDialog::onControllerButtonEvent(const SDL_GamepadButtonEvent& arg)
    {
        if (arg.button == SDL_GAMEPAD_BUTTON_SOUTH)
        {
            onWaitButtonClicked(mWaitButton);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_EAST)
            onCancelButtonClicked(mCancelButton);
        else if (arg.button == SDL_GAMEPAD_BUTTON_WEST && mUntilHealedButton->getVisible())
        {
            onUntilHealedButtonClicked(mUntilHealedButton);
            MWBase::Environment::get().getWindowManager()->playSound(ESM::RefId::stringRefId("Menu Click"));
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowUp, 0, false);
        else if (arg.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
        {
            mHourSlider->setScrollPosition(0);
            onHourSliderChangedPosition(mHourSlider, mHourSlider->getScrollPosition());
        }
        else if (arg.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
        {
            mHourSlider->setScrollPosition(mHourSlider->getScrollRange() - 1);
            onHourSliderChangedPosition(mHourSlider, mHourSlider->getScrollPosition());
        }

        return true;
    }

    void WaitDialog::stopWaiting()
    {
        MWBase::Environment::get().getWindowManager()->fadeScreenIn(0.2f);
        mProgressBar.setVisible(false);
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Rest);
        mTimeAdvancer.stop();
    }

    void WaitDialog::wakeUp()
    {
        mSleeping = false;
        if (mInterruptAt != -1)
            onWaitingInterrupted();
        else
            stopWaiting();
    }

}
