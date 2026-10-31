#include "listbox.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

#include <MyGUI_Button.h>
#include <MyGUI_Prerequest.h>
#include <MyGUI_ScrollBar.h>

// **A port to one MyGUI's private members**, which another release may rename, retype or lay
// out anew: MyGUI's own `setItemHeight` replaces it once the build requires a release that has it.
static_assert(MYGUI_VERSION == MYGUI_DEFINE_VERSION(3, 4, 3), "Gui::ListBox reaches MyGUI 3.4.3's privates");

namespace Gui
{
    namespace
    {
        // **A private member named where access is not checked**: an explicit instantiation's
        // arguments may name any member (C++20 [temp.explicit]), and the instantiation defines a
        // friend that hands the member's pointer to whoever names the tag.
        template <class Tag, typename Tag::Type member>
        struct Reach
        {
            friend constexpr typename Tag::Type reached(Tag) { return member; }
        };

        struct HeightLine
        {
            using Type = int MyGUI::ListBox::*;
            friend constexpr Type reached(HeightLine);
        };

        struct TopIndex
        {
            using Type = int MyGUI::ListBox::*;
            friend constexpr Type reached(TopIndex);
        };

        struct OffsetTop
        {
            using Type = int MyGUI::ListBox::*;
            friend constexpr Type reached(OffsetTop);
        };

        struct RangeIndex
        {
            using Type = int MyGUI::ListBox::*;
            friend constexpr Type reached(RangeIndex);
        };

        struct Scroll
        {
            using Type = MyGUI::ScrollBar* MyGUI::ListBox::*;
            friend constexpr Type reached(Scroll);
        };

        struct Lines
        {
            using Type = std::vector<MyGUI::Button*> MyGUI::ListBox::*;
            friend constexpr Type reached(Lines);
        };

        template struct Reach<HeightLine, &MyGUI::ListBox::mHeightLine>;
        template struct Reach<TopIndex, &MyGUI::ListBox::mTopIndex>;
        template struct Reach<OffsetTop, &MyGUI::ListBox::mOffsetTop>;
        template struct Reach<RangeIndex, &MyGUI::ListBox::mRangeIndex>;
        template struct Reach<Scroll, &MyGUI::ListBox::mWidgetScroll>;
        template struct Reach<Lines, &MyGUI::ListBox::mWidgetLines>;

        /// A line of text, and the pixel above and below it the skins were given.
        int lineHeightOf(const FontUsers& users)
        {
            return users.getTextHeight() + 2;
        }
    }

    void ListBox::initialiseOverride()
    {
        MyGUI::ListBox::initialiseOverride();

        mHeightStated = isUserString("HeightLine");
        if (!mHeightStated)
            setItemHeight(lineHeightOf(getUsers()));
    }

    void ListBox::refit()
    {
        if (!mHeightStated)
            setItemHeight(lineHeightOf(getUsers()));
    }

    void ListBox::setItemHeight(int height)
    {
        height = std::max(1, height);
        int& heightLine = this->*reached(HeightLine{});
        if (heightLine == height)
            return;

        const int position = this->*reached(TopIndex{}) * heightLine + this->*reached(OffsetTop{});
        heightLine = height;

        if (MyGUI::ScrollBar* const scroll = this->*reached(Scroll{}); scroll != nullptr)
            scroll->setScrollPage(static_cast<std::size_t>(heightLine));
        updateScroll();

        for (MyGUI::Button* const line : this->*reached(Lines{}))
            line->setSize(_getClientWidget()->getWidth(), heightLine);

        _setScrollView(static_cast<std::size_t>(std::clamp(position, 0, std::max(0, this->*reached(RangeIndex{})))));
        updateLine(true);
    }

    int ListBox::getItemHeight() const
    {
        return this->*reached(HeightLine{});
    }
}
