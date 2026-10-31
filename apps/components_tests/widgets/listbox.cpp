#include <cstddef>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <MyGUI_Button.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_ResourceManager.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/fontloader/interfacefonts.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/widgets/listbox.hpp>

namespace Gui
{
    namespace
    {
        /// A list of `items` items, 200 high, with the shipped list skin.
        MyGUI::ListBox* makeList(std::string_view skin, std::size_t items)
        {
            MyGUI::ListBox* const list = MyGUI::Gui::getInstance().createWidget<MyGUI::ListBox>(
                skin, MyGUI::IntCoord(10, 10, 300, 200), MyGUI::Align::Default, "Windows");
            for (std::size_t at = 0; at < items; ++at)
                list->addItem("Item " + std::to_string(at));
            return list;
        }

        /// Every visible line's height and its top, in the list's client.
        void expectLinesAt(MyGUI::ListBox& list, int height, int offset)
        {
            MyGUI::Widget* const client = list.getClientWidget();
            std::size_t shown = 0;
            for (std::size_t at = 0; at < client->getChildCount(); ++at)
            {
                MyGUI::Widget* const line = client->getChildAt(at);
                if (!line->getVisible())
                    continue;
                EXPECT_EQ(line->getHeight(), height) << "line " << at;
                EXPECT_EQ(line->getTop(), static_cast<int>(at) * height - offset) << "line " << at;
                ++shown;
            }
            EXPECT_GT(shown, 0u) << "the list shows no line";
        }

        /// **A list's lines stand a line of text high, and follow a fit to another size.** In an
        /// interface at 16 points, a list made by MyGUI's name is the fork's, and its lines stand
        /// 16 + 2 high. Its client is the list's 200 less the skin's 4 above and below: 192. With
        /// forty items it scrolls over 40 × 18 − 192 = 528 pixels, and it is scrolled to 180, its
        /// tenth line's top, with its twelfth item selected. After a fit to 18 points every line
        /// stands 20 high, the range is 40 × 20 − 192 = 608, the pixel position 180 is kept, which
        /// puts line 9 at the top, 180 / 20, with no part of a line above it, and the selection
        /// stays. The range ends at 608: a scroll to 609 is refused, and one to 608 stands the top
        /// line 8 above the client. A list whose skin states its own line height keeps its 25
        /// through the same fit.
        TEST(ListBoxTest, aListsLinesStandALineOfTextHighAndFollowAFitToAnotherSize)
        {
            TestingOpenMW::VFSTestFile statedSkin(R"(<MyGUI type="Resource" version="1.1">
                <Resource type="ResourceLayout" name="StatedList" size="516 516" align="Left Top">
                    <Widget type="Widget" position="0 0 516 516" name="Root">
                        <UserString key="SkinLine" value="MW_ListLine"/>
                        <UserString key="HeightLine" value="25"/>
                        <Widget type="Widget" skin="" position="3 4 493 508" align="Stretch" name="Client"/>
                    </Widget>
                </Resource>
            </MyGUI>)");
            Rtx::Testing::GuiInstance gui({ .mFontSize = 16,
                .mOverrides = { { VFS::Path::Normalized("mygui/openmw_stated_list.skin.xml"), &statedSkin } } });
            MyGUI::ResourceManager::getInstance().load("openmw_list.skin.xml");
            MyGUI::ResourceManager::getInstance().load("openmw_stated_list.skin.xml");

            MyGUI::ListBox* const list = makeList("MW_List", 40);
            ListBox* const fork = list->castType<ListBox>(false);
            ASSERT_NE(fork, nullptr) << "MyGUI's own list stands in its factory";
            ListBox& stated = *makeList("StatedList", 40)->castType<ListBox>();
            ASSERT_EQ(list->getClientWidget()->getHeight(), 192);
            ASSERT_EQ(fork->getItemHeight(), 18);
            ASSERT_EQ(stated.getItemHeight(), 25);
            list->setIndexSelected(11);
            list->setScrollPosition(180);
            ASSERT_EQ(list->getOptimalHeight(), 8 + 40 * 18);
            expectLinesAt(*list, 18, 0);

            FontFit larger = gui.getFonts().getFit();
            larger.mFontSize = 18;
            gui.getFonts().fit(larger);

            EXPECT_EQ(fork->getItemHeight(), 20);
            EXPECT_EQ(list->getOptimalHeight(), 8 + 40 * 20);
            expectLinesAt(*list, 20, 0);
            EXPECT_EQ(list->getIndexSelected(), 11u);
            EXPECT_EQ(list->getClientWidget()->getChildAt(0)->castType<MyGUI::Button>()->getCaption(), "Item 9");
            EXPECT_EQ(list->getClientWidget()->getChildAt(2)->castType<MyGUI::Button>()->getStateSelected(), true)
                << "line 2 shows item 11, the selected one";
            EXPECT_EQ(stated.getItemHeight(), 25);

            // The range's last position is 608, where line 30 stands 8 above the client's top.
            list->setScrollPosition(609);
            expectLinesAt(*list, 20, 0);
            list->setScrollPosition(608);
            expectLinesAt(*list, 20, 8);
        }
    }
}
