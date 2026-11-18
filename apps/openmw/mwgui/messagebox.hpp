#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "windowbase.hpp"

namespace MyGUI
{
    class Widget;
    class Button;
    class EditBox;
}

namespace MWGui
{
    class InteractiveMessageBox;
    class MessageBoxManager;
    class MessageBox;
    class MessageBoxManager
    {
    public:
        explicit MessageBoxManager(float timePerChar);
        ~MessageBoxManager();
        void onFrame(float frameDuration);
        void createMessageBox(std::string_view message, bool stat = false);
        void removeStaticMessageBox();
        bool createInteractiveMessageBox(std::string_view message, const std::vector<std::string>& buttons,
            bool immediate = false, std::int32_t defaultFocus = -1);
        bool isInteractiveMessageBox();

        std::size_t getMessagesCount();

        const InteractiveMessageBox* getInteractiveMessageBox() const { return mInterMessageBoxe.get(); }

        /// Remove all message boxes
        void clear();

        bool removeMessageBox(MessageBox* msgbox);

        /// @param reset Reset the pressed button to -1 after reading it.
        std::int32_t readPressedButton(bool reset = true);

        void resetInteractiveMessageBox();

        void setLastButtonPressed(std::int32_t index);

        typedef MyGUI::delegates::MultiDelegate<std::int32_t> EventHandle_Int;

        // Note: this delegate unassigns itself after it was fired, i.e. works once.
        EventHandle_Int eventButtonPressed;

        void onButtonPressed(std::int32_t button)
        {
            eventButtonPressed(button);
            eventButtonPressed.clear();
        }

        void setVisible(bool value);

        const std::vector<std::unique_ptr<MessageBox>>& getActiveMessageBoxes() const;

    private:
        std::vector<std::unique_ptr<MessageBox>> mMessageBoxes;
        std::unique_ptr<InteractiveMessageBox> mInterMessageBoxe;
        MessageBox* mStaticMessageBox;
        float mMessageBoxSpeed;
        std::int32_t mLastButtonPressed;
        bool mVisible = true;
    };

    class MessageBox : public Layout
    {
    public:
        MessageBox(MessageBoxManager& parMessageBoxManager, std::string_view message);
        const std::string& getMessage() { return mMessage; }
        std::int32_t getHeight();
        void update(std::int32_t height);
        void setVisible(bool value) override;

        float mCurrentTime;
        float mMaxTime;

    protected:
        MessageBoxManager& mMessageBoxManager;
        std::string mMessage;
        MyGUI::EditBox* mMessageWidget{};
        std::int32_t mBottomPadding;
        std::int32_t mNextBoxPadding;
    };

    class InteractiveMessageBox : public WindowModal
    {
    public:
        InteractiveMessageBox(MessageBoxManager& parMessageBoxManager, const std::string& message,
            const std::vector<std::string>& buttons, bool immediate, std::size_t defaultFocus);
        void mousePressed(MyGUI::Widget* widget);
        std::int32_t readPressedButton();

        MyGUI::Widget* getDefaultKeyFocus() override;

        bool exit() override { return false; }

        bool mMarkedToDelete;

        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

    private:
        void buttonActivated(MyGUI::Widget* widget);

        MessageBoxManager& mMessageBoxManager;
        MyGUI::EditBox* mMessageWidget{};
        MyGUI::Widget* mButtonsWidget{};
        std::vector<MyGUI::Button*> mButtons;

        std::int32_t mButtonPressed;
        std::size_t mDefaultFocus;
        bool mImmediate;
        std::size_t mControllerFocus = 0;
    };

}
