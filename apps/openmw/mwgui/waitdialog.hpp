#pragma once

#include <cstddef>
#include <cstdint>

#include <components/esm/refid.hpp>

#include "timeadvancer.hpp"
#include "windowbase.hpp"

namespace MWGui
{

    class WaitDialogProgressBar : public WindowBase
    {
    public:
        WaitDialogProgressBar();

        void onOpen() override;

        void setProgress(std::int32_t cur, std::int32_t total);

    protected:
        MyGUI::ProgressBar* mProgressBar{};
        MyGUI::TextBox* mProgressText{};
    };

    class WaitDialog : public WindowBase
    {
    public:
        WaitDialog();

        void setPtr(const MWWorld::Ptr& ptr) override;

        void onOpen() override;

        bool exit() override;

        void clear() override;

        void onFrame(float dt) override;
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

        bool getSleeping() { return mTimeAdvancer.isRunning() && mSleeping; }
        void wakeUp();
        void autosave();

        WindowBase* getProgressBar() { return &mProgressBar; }

        std::string_view getWindowIdForLua() const override { return "WaitDialog"; }

        ControllerButtons* getControllerButtons() override;

    protected:
        MyGUI::TextBox* mDateTimeText{};
        MyGUI::TextBox* mRestText{};
        MyGUI::TextBox* mHourText{};
        MyGUI::Button* mUntilHealedButton{};
        MyGUI::Button* mWaitButton{};
        MyGUI::Button* mCancelButton{};
        MyGUI::ScrollBar* mHourSlider{};

        TimeAdvancer mTimeAdvancer;
        bool mSleeping;
        std::int32_t mHours;
        std::int32_t mManualHours; // stores the hours to rest selected via slider
        float mFadeTimeRemaining;

        std::int32_t mInterruptAt;
        ESM::RefId mInterruptCreatureList;

        WaitDialogProgressBar mProgressBar;

        void onUntilHealedButtonClicked(MyGUI::Widget* sender);
        void onWaitButtonClicked(MyGUI::Widget* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);
        void onHourSliderChangedPosition(MyGUI::ScrollBar* sender, std::size_t position);
        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);

        void onWaitingProgressChanged(std::int32_t cur, std::int32_t total);
        void onWaitingInterrupted();
        void onWaitingFinished();

        void setCanRest(bool canRest);

        void startWaiting(std::int32_t hoursToWait);
        void stopWaiting();
    };

}
