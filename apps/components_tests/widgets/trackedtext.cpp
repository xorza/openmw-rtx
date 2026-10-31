#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_ChildSkinInfo.h>
#include <MyGUI_FactoryManager.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_IFont.h>
#include <MyGUI_ISubWidgetText.h>
#include <MyGUI_ResourceManager.h>
#include <MyGUI_ResourceSkin.h>
#include <MyGUI_TextBox.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/fontloader/fontloader.hpp>
#include <components/myguirtx/slottexture.hpp>
#include <components/settings/values.hpp>
#include <components/widgets/textregistry.hpp>
#include <components/widgets/trackedtext.hpp>

namespace Gui
{
    namespace
    {
        MyGUI::TextBox* makeText(std::string_view font, int top)
        {
            MyGUI::TextBox* const text = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>(
                "SandText", MyGUI::IntCoord(10, top, 300, 30), MyGUI::Align::Default, "Windows");
            text->setFontName(font);
            text->setCaption("Abc");
            return text;
        }

        std::uint32_t atlasOf(std::string_view font)
        {
            return dynamic_cast<const MyGUIRtx::SlotTexture&>(
                *MyGUI::FontManager::getInstance().getByName(font)->getTextureFont())
                .getSlot()
                .get();
        }

        /// **The registry holds exactly the texts there are**: a text box's skin makes one, three
        /// make three, and each one destroyed leaves, the one in the middle included.
        TEST(TextRegistryTest, theRegistryHoldsExactlyTheTextsThereAre)
        {
            Rtx::Testing::GuiInstance gui;
            const std::size_t before = gui.getTexts().size();

            std::vector<MyGUI::TextBox*> texts;
            for (int at = 0; at < 3; ++at)
                texts.push_back(makeText("DefaultFont", 10 + 40 * at));
            EXPECT_EQ(gui.getTexts().size(), before + 3);

            MyGUI::Gui::getInstance().destroyWidget(texts[1]);
            EXPECT_EQ(gui.getTexts().size(), before + 2);

            MyGUI::Gui::getInstance().destroyWidget(texts[0]);
            MyGUI::Gui::getInstance().destroyWidget(texts[2]);
            EXPECT_EQ(gui.getTexts().size(), before);
        }

        /// **After a fit and a rebind every text draws against its font's new atlas, where its glyphs
        /// now are**: texts in two fonts, drawn once and then fitted to a raster of 3, draw two
        /// batches, one against each font's atlas as it now is and neither against one the fit
        /// replaced, and the first letter's corners are the letter's place in the new atlas. A
        /// replaced atlas's address may be its replacement's, so the corners are what tell a text
        /// that was rebound from one that was not.
        TEST(TextRegistryTest, afterAFitAndARebindEveryTextDrawsAgainstItsFontsNewAtlas)
        {
            Rtx::Testing::GuiInstance gui;
            makeText("DefaultFont", 10);
            makeText("MonoFont", 50);
            gui.draw();
            const std::set<std::uint32_t> replaced{ atlasOf("DefaultFont"), atlasOf("MonoFont") };

            // To 3 and not 2: a default font's atlas twice the side with every glyph twice the size
            // puts each glyph at the same fraction of it, and would draw old corners as new ones.
            gui.getFonts().fit(FontFit{ .mRaster = 3.0f, .mView = MyGUI::IntSize(341, 256) });
            gui.getTexts().rebind();

            const Rtx::Testing::DrawnGui drawn = gui.draw();
            std::set<std::uint32_t> drawnAgainst;
            for (const Rtx::GuiBatch& batch : drawn.mBatches)
                drawnAgainst.insert(batch.mTexture.get());
            EXPECT_EQ(drawnAgainst, (std::set<std::uint32_t>{ atlasOf("DefaultFont"), atlasOf("MonoFont") }));
            for (const std::uint32_t slot : replaced)
                EXPECT_FALSE(drawnAgainst.contains(slot)) << "a text drew a replaced atlas";

            const MyGUI::FloatRect a
                = MyGUI::FontManager::getInstance().getByName("DefaultFont")->getGlyphInfo('A')->uvRect;
            for (const Rtx::GuiBatch& batch : drawn.mBatches)
            {
                if (batch.mTexture.get() != atlasOf("DefaultFont"))
                    continue;
                std::set<std::pair<float, float>> corners;
                for (std::uint32_t at = batch.mFirstVertex; at < batch.mFirstVertex + 6; ++at)
                    corners.emplace(drawn.mVertices[at].mU, drawn.mVertices[at].mV);
                EXPECT_EQ(corners,
                    (std::set<std::pair<float, float>>{
                        { a.left, a.top }, { a.right, a.top }, { a.left, a.bottom }, { a.right, a.bottom } }));
            }
        }

        /// **Every text a shipped skin makes is the registry's**: a text box made with each skin
        /// MyGUI has loaded, whichever basis its text is, draws through a tracked text, so none of
        /// them can miss a rebind. A skin with a child of the game's own widget types is left out,
        /// which `components-tests` cannot make.
        TEST(TextRegistryTest, everyTextAShippedSkinMakesIsTheRegistrys)
        {
            Rtx::Testing::GuiInstance gui;
            std::size_t skins = 0;
            MyGUI::ResourceManager::EnumeratorPtr resources = MyGUI::ResourceManager::getInstance().getEnumerator();
            while (resources.next())
            {
                const auto* const skinned = resources.current().second->castType<MyGUI::ResourceSkin>(false);
                if (skinned == nullptr)
                    continue;

                // A skin whose children are the game's own widgets is not one this binary can make.
                bool makeable = true;
                for (const MyGUI::ChildSkinInfo& child : skinned->getChild())
                    makeable &= MyGUI::FactoryManager::getInstance().isFactoryExist("Widget", child.type);
                if (!makeable)
                    continue;
                const std::string skin = resources.current().first;
                MyGUI::TextBox* const box = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>(
                    skin, MyGUI::IntCoord(0, 0, 100, 20), MyGUI::Align::Default, "Windows");
                if (MyGUI::ISubWidgetText* const text = box->getSubWidgetText())
                {
                    ++skins;
                    EXPECT_TRUE(text->castType<TrackedText>(false) != nullptr
                        || text->castType<TrackedSimpleText>(false) != nullptr)
                        << skin << " makes a text the registry does not know";
                }
                MyGUI::Gui::getInstance().destroyWidget(box);
            }
            EXPECT_GT(skins, 10u) << "next to no text skin loaded, so the sweep found nothing";
        }

        /// **A text's height never reads the raster.** A text box's height is the font size it is
        /// given, 16, before and after a fit to a raster of 2; and a text with no height of its own
        /// takes the interface's font size too, after a fit to 2 and in an interface loaded at 2,
        /// where MyGUI's would take the font's own height, 32 raster pixels there.
        TEST(TextRegistryTest, aTextsHeightNeverReadsTheRaster)
        {
            const int size = Settings::gui().mFontSize;
            {
                Rtx::Testing::GuiInstance gui;
                MyGUI::TextBox* const sized = makeText("DefaultFont", 10);
                MyGUI::TextBox* const unsized = makeText("DefaultFont", 50);
                unsized->setFontHeight(0);
                ASSERT_EQ(sized->getTextSize().height, size);
                ASSERT_EQ(unsized->getTextSize().height, size);

                gui.getFonts().fit(FontFit{ .mRaster = 2.0f, .mView = MyGUI::IntSize(512, 384) });
                gui.getTexts().rebind();

                EXPECT_EQ(MyGUI::FontManager::getInstance().getByName("DefaultFont")->getDefaultHeight(), 32);
                EXPECT_EQ(sized->getTextSize().height, size);
                EXPECT_EQ(unsized->getTextSize().height, size) << "after a fit to 2";
            }

            Rtx::Testing::GuiInstance gui({ .mScale = 2.0f });
            MyGUI::TextBox* const unsized = makeText("DefaultFont", 10);
            unsized->setFontHeight(0);
            EXPECT_EQ(MyGUI::FontManager::getInstance().getByName("DefaultFont")->getDefaultHeight(), 32);
            EXPECT_EQ(unsized->getTextSize().height, size) << "in an interface loaded at 2";
        }
    }
}
