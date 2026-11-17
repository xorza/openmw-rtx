#include "countdialog.hpp"

#include <cstddef>
#include <cstdint>

#include <MyGUI_Button.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ScrollBar.h>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <components/widgets/numericeditbox.hpp>

namespace MWGui
{
    CountDialog::CountDialog()
        : WindowModal("openmw_count_window.layout")
    {
        getWidget(mSlider, "CountSlider");
        getWidget(mItemEdit, "ItemEdit");
        getWidget(mItemText, "ItemText");
        getWidget(mLabelText, "LabelText");
        getWidget(mOkButton, "OkButton");
        getWidget(mCancelButton, "CancelButton");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &CountDialog::onCancelButtonClicked);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &CountDialog::onOkButtonClicked);
        mItemEdit->eventValueChanged += MyGUI::newDelegate(this, &CountDialog::onEditValueChanged);
        mSlider->eventScrollChangePosition += MyGUI::newDelegate(this, &CountDialog::onSliderMoved);
        // make sure we read the enter key being pressed to accept multiple items
        mItemEdit->eventEditSelectAccept += MyGUI::newDelegate(this, &CountDialog::onEnterKeyPressed);

        mControllerButtons.mA = "#{Interface:OK}";
        mControllerButtons.mB = "#{Interface:Cancel}";
    }

    void CountDialog::openCountDialog(const std::string& item, const std::string& message, const std::int32_t maxCount)
    {
        setVisible(true);

        mLabelText->setCaptionWithReplacing(message);

        MyGUI::IntSize viewSize = MyGUI::RenderManager::getInstance().getViewSize();

        mSlider->setScrollRange(static_cast<std::size_t>(maxCount));
        mItemText->setCaptionWithReplacing(item);

        std::int32_t width = std::max(mItemText->getTextSize().width + 160, 320);
        setCoord(viewSize.width / 2 - width / 2, viewSize.height / 2 - mMainWidget->getHeight() / 2, width,
            mMainWidget->getHeight());

        // by default, the text edit field has the focus of the keyboard
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mItemEdit);

        mSlider->setScrollPosition(static_cast<std::size_t>(maxCount - 1));

        mItemEdit->setMinValue(1);
        mItemEdit->setMaxValue(maxCount);
        mItemEdit->setValue(maxCount);
    }

    void CountDialog::setCount(std::int32_t count)
    {
        count = std::clamp(count, 1, static_cast<std::int32_t>(mSlider->getScrollRange()));
        mSlider->setScrollPosition(static_cast<std::size_t>(count - 1));
        mItemEdit->setValue(count);
    }

    void CountDialog::onCancelButtonClicked(MyGUI::Widget* /*sender*/)
    {
        setVisible(false);
    }

    void CountDialog::onOkButtonClicked(MyGUI::Widget* /*sender*/)
    {
        // The order here matters. Hide the dialog first so the OK event tooltips reappear.
        setVisible(false);
        eventOkClicked(nullptr, mSlider->getScrollPosition() + 1);
    }

    // essentially duplicating what the OK button does if user presses
    // Enter key
    void CountDialog::onEnterKeyPressed(MyGUI::EditBox* sender)
    {
        onOkButtonClicked(sender);

        // To do not spam onEnterKeyPressed() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void CountDialog::onEditValueChanged(std::int32_t value)
    {
        mSlider->setScrollPosition(static_cast<std::size_t>(value - 1));
    }

    void CountDialog::onSliderMoved(MyGUI::ScrollBar* sender, std::size_t position)
    {
        mItemEdit->setValue(static_cast<std::int32_t>(position + 1));
    }

    bool CountDialog::onControllerButtonEvent(const SDL_GamepadButtonEvent& arg)
    {
        if (arg.button == SDL_GAMEPAD_BUTTON_SOUTH)
            onOkButtonClicked(mOkButton);
        else if (arg.button == SDL_GAMEPAD_BUTTON_EAST)
            onCancelButtonClicked(mCancelButton);
        else if (arg.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)
            setCount(1);
        else if (arg.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)
            setCount(static_cast<std::int32_t>(mSlider->getScrollRange()));
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowDown, 0, false);
        else if (arg.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
            MWBase::Environment::get().getWindowManager()->injectKeyPress(MyGUI::KeyCode::ArrowUp, 0, false);

        return true;
    }
}
