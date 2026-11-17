#pragma once

#include <cstdint>

#include <apps/openmw/mwworld/ptr.hpp>
#include <components/widgets/imagebutton.hpp>

#include "windowbase.hpp"

namespace MWGui
{
    class BookWindow : public BookWindowBase
    {
    public:
        BookWindow();

        void setPtr(const MWWorld::Ptr& book) override;
        void setInventoryAllowed(bool allowed);

        void onResChange(std::int32_t, std::int32_t) override { center(); }
        bool onControllerButtonEvent(const SDL_GamepadButtonEvent& arg) override;

        std::string_view getWindowIdForLua() const override { return "Book"; }
        ControllerButtons* getControllerButtons() override;

    protected:
        void onNextPageButtonClicked(MyGUI::Widget* sender);
        void onPrevPageButtonClicked(MyGUI::Widget* sender);
        void onCloseButtonClicked(MyGUI::Widget* sender);
        void onTakeButtonClicked(MyGUI::Widget* sender);
        void onMouseWheel(MyGUI::Widget* sender, std::int32_t rel);
        void setTakeButtonShow(bool show);

        void onKeyButtonPressed(MyGUI::Widget* sender, MyGUI::KeyCode key, MyGUI::Char character);

        void nextPage();
        void prevPage();

        void updatePages();
        void clearPages();

    private:
        typedef std::pair<std::int32_t, std::int32_t> Page;
        typedef std::vector<Page> Pages;

        Gui::ImageButton* mCloseButton{};
        Gui::ImageButton* mTakeButton{};
        Gui::ImageButton* mNextPageButton{};
        Gui::ImageButton* mPrevPageButton{};

        MyGUI::TextBox* mLeftPageNumber{};
        MyGUI::TextBox* mRightPageNumber{};
        MyGUI::Widget* mLeftPage{};
        MyGUI::Widget* mRightPage{};

        std::uint32_t mCurrentPage; // 0 is first page
        Pages mPages;

        MWWorld::Ptr mBook;

        bool mTakeButtonShow;
        bool mTakeButtonAllowed;
    };

}
