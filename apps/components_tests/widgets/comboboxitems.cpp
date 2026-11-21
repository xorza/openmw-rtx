#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_ComboBox.h>
#include <MyGUI_Gui.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ResourceManager.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/pathutil.hpp>
#include <components/widgets/comboboxitems.hpp>

namespace Gui
{
    namespace
    {
        MyGUI::ComboBox& makeComboBox(const std::vector<std::string>& items)
        {
            auto* const box = MyGUI::Gui::getInstance().createWidget<MyGUI::ComboBox>(
                "TestComboBox", MyGUI::IntCoord(10, 10, 100, 26), MyGUI::Align::Default, "Windows");
            for (const std::string& item : items)
                box->addItem(item);
            return *box;
        }

        /// One step of the wheel down over `box`'s text, where MyGUI hooks its wheel, with the key focus it asks for.
        void wheelDown(MyGUI::ComboBox& box)
        {
            MyGUI::InputManager::getInstance().setKeyFocusWidget(&box);
            box.getClientWidget()->_riseMouseWheel(-1);
        }

        /// **The items replaced, the selected one stays selected, and the wheel steps on from it**: alchemy's filter
        /// lists the player's ingredients again after each potion, and its wheel then went to the first item, not the
        /// one after the one shown (upstream #8143). With "Burden" of four selected and the list put back whole, it is
        /// still the selection, at 1, and a step down selects "Fortify Health", at 2. MyGUI's own `removeAllItems`
        /// and `addItem` keep "Burden" as the caption with none selected, and the same step selects the first item.
        /// A selection the new items lack leaves none, and none selected stays none.
        TEST(ComboBoxItemsTest, theSelectedItemStaysSelectedAndTheWheelStepsOnFromIt)
        {
            // A combo box as the game's `MW_ComboBox` lays one out, from the skins the test interface has.
            TestingOpenMW::VFSTestFile skin(R"(<MyGUI type="Resource" version="1.1">
                <Resource type="ResourceLayout" name="TestComboBox" version="3.2.0">
                    <Widget type="Widget" skin="" position="0 0 100 26" name="Root">
                        <Widget type="TextBox" skin="SandText" position="6 3 71 20" align="Stretch" name="Client"/>
                        <Widget type="ListBox" skin="MW_PopupListNoTransp" position="0 26 100 200" style="Popup"
                            layer="Popup" name="List">
                            <Property key="Visible" value="false"/>
                        </Widget>
                    </Widget>
                </Resource>
            </MyGUI>)");
            Rtx::Testing::GuiInstance gui(
                { .mOverrides = { { VFS::Path::Normalized("mygui/test_combobox.skin.xml"), &skin } } });
            MyGUI::ResourceManager::getInstance().load("openmw_list.skin.xml");
            MyGUI::ResourceManager::getInstance().load("test_combobox.skin.xml");
            const std::vector<std::string> effects{ "Blind", "Burden", "Fortify Health", "Restore Fatigue" };

            MyGUI::ComboBox& kept = makeComboBox(effects);
            kept.setIndexSelected(1);
            replaceComboBoxItems(kept, effects);
            EXPECT_EQ(kept.getIndexSelected(), 1u);
            EXPECT_EQ(kept.getCaption(), "Burden");
            wheelDown(kept);
            EXPECT_EQ(kept.getIndexSelected(), 2u);
            EXPECT_EQ(kept.getCaption(), "Fortify Health");

            MyGUI::ComboBox& forgotten = makeComboBox(effects);
            forgotten.setIndexSelected(1);
            forgotten.removeAllItems();
            for (const std::string& effect : effects)
                forgotten.addItem(effect);
            EXPECT_EQ(forgotten.getIndexSelected(), MyGUI::ITEM_NONE);
            EXPECT_EQ(forgotten.getCaption(), "Burden");
            wheelDown(forgotten);
            EXPECT_EQ(forgotten.getIndexSelected(), 0u) << "MyGUI's own steps from the first item";

            MyGUI::ComboBox& gone = makeComboBox(effects);
            gone.setIndexSelected(1);
            const std::vector<std::string> fewer{ "Blind", "Fortify Health" };
            replaceComboBoxItems(gone, fewer);
            EXPECT_EQ(gone.getIndexSelected(), MyGUI::ITEM_NONE);
            EXPECT_EQ(gone.getItemCount(), 2u);

            MyGUI::ComboBox& none = makeComboBox(effects);
            replaceComboBoxItems(none, effects);
            EXPECT_EQ(none.getIndexSelected(), MyGUI::ITEM_NONE);
        }
    }
}
