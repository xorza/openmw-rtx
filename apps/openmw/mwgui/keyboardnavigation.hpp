#pragma once

#include <cstdint>

#include <MyGUI_IUnlinkWidget.h>
#include <MyGUI_KeyCode.h>

namespace MWGui
{

    class KeyboardNavigation : public MyGUI::IUnlinkWidget
    {
    public:
        KeyboardNavigation();
        ~KeyboardNavigation() override;
        KeyboardNavigation(const KeyboardNavigation&) = delete;
        KeyboardNavigation& operator=(const KeyboardNavigation&) = delete;

        /// @return Was the key handled by this class?
        bool injectKeyPress(MyGUI::KeyCode key, std::uint32_t text, bool repeat);

        void saveFocus(std::int32_t mode);
        void restoreFocus(std::int32_t mode);

        void _unlinkWidget(MyGUI::Widget* widget) override;

        void onFrame();

        /// Set a key focus widget for this window, if one isn't already set.
        void setDefaultFocus(MyGUI::Widget* window, MyGUI::Widget* defaultFocus);

        void setModalWindow(MyGUI::Widget* window);

        void setEnabled(bool enabled);

    private:
        bool switchFocus(std::int32_t direction, bool wrap);

        bool selectFirstWidget();

        /// Send button press event to focused button
        bool accept();

        std::map<std::int32_t, MyGUI::Widget*> mKeyFocus;

        MyGUI::Widget* mCurrentFocus;
        MyGUI::Widget* mModalWindow;

        bool mEnabled;
    };

}
