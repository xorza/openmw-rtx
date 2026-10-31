#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ResourceManager.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/widgets/settlingscrollbar.hpp>

namespace Gui
{
    namespace
    {
        /// A bar of 51 positions, 200 units wide, in the skin the settings window's sliders use, and
        /// what it said of itself.
        struct Bar
        {
            Rtx::Testing::GuiInstance mGui;
            SettlingScrollBar* mBar = nullptr;
            std::vector<std::size_t> mMoved;
            std::vector<std::size_t> mSettled;

            Bar()
            {
                MyGUI::ResourceManager::getInstance().load("openmw_list.skin.xml");
                mBar = MyGUI::Gui::getInstance().createWidget<SettlingScrollBar>(
                    "MW_HScroll", MyGUI::IntCoord(0, 0, 200, 14), MyGUI::Align::Default, "Windows");
                mBar->setScrollRange(51);
                mBar->eventScrollChangePosition += MyGUI::newDelegate(this, &Bar::moved);
                mBar->eventScrollSettled += MyGUI::newDelegate(this, &Bar::settled);
            }

            void moved(MyGUI::ScrollBar*, std::size_t position) { mMoved.push_back(position); }
            void settled(MyGUI::ScrollBar*, std::size_t position) { mSettled.push_back(position); }
        };

        /// **A drag moves the bar at every step and settles it once, where the release left it**:
        /// the thumb pressed at the bar's start and dragged across moves the position several
        /// times and settles nothing until the button goes up, then settles it once, at the
        /// position the last move reached.
        TEST(SettlingScrollBarTest, aDragSettlesOnceWhereTheReleaseLeftIt)
        {
            Bar bar;
            MyGUI::InputManager& input = MyGUI::InputManager::getInstance();
            input.injectMouseMove(22, 7, 0);
            input.injectMousePress(22, 7, MyGUI::MouseButton::Left);
            for (int x = 40; x <= 120; x += 20)
                input.injectMouseMove(x, 7, 0);
            ASSERT_GT(bar.mMoved.size(), 1u) << "the drag did not move the bar";
            EXPECT_TRUE(bar.mSettled.empty()) << "settled while the drag went on";

            input.injectMouseRelease(120, 7, MyGUI::MouseButton::Left);

            ASSERT_EQ(bar.mSettled, std::vector<std::size_t>{ bar.mBar->getScrollPosition() });
            EXPECT_EQ(bar.mSettled.front(), bar.mMoved.back());
        }

        /// **Each step of the wheel over the thumb settles the bar**, one position a step at a wheel
        /// page of one. The skin's other parts are buttons that take the wheel and do nothing with
        /// it, in MyGUI's bar as in this one.
        TEST(SettlingScrollBarTest, eachStepOfTheWheelOverTheThumbSettlesTheBar)
        {
            Bar bar;
            bar.mBar->setScrollWheelPage(1);
            MyGUI::InputManager& input = MyGUI::InputManager::getInstance();
            input.injectMouseMove(22, 7, 0);
            input.injectMouseMove(22, 7, -1);
            input.injectMouseMove(22, 7, -2);

            EXPECT_EQ(bar.mSettled, (std::vector<std::size_t>{ 1, 2 }));
        }
    }
}
