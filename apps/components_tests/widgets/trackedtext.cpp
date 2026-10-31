#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_Button.h>
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
#include <components/fontloader/interfacefonts.hpp>
#include <components/myguirtx/slottexture.hpp>
#include <components/widgets/box.hpp>
#include <components/widgets/fontusers.hpp>
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
        TEST(FontUsersTest, theRegistryHoldsExactlyTheTextsThereAre)
        {
            Rtx::Testing::GuiInstance gui;
            const std::size_t before = gui.getFonts().getTexts().size();

            std::vector<MyGUI::TextBox*> texts;
            for (int at = 0; at < 3; ++at)
                texts.push_back(makeText("DefaultFont", 10 + 40 * at));
            EXPECT_EQ(gui.getFonts().getTexts().size(), before + 3);

            MyGUI::Gui::getInstance().destroyWidget(texts[1]);
            EXPECT_EQ(gui.getFonts().getTexts().size(), before + 2);

            MyGUI::Gui::getInstance().destroyWidget(texts[0]);
            MyGUI::Gui::getInstance().destroyWidget(texts[2]);
            EXPECT_EQ(gui.getFonts().getTexts().size(), before);
        }

        /// **After a fit every text draws against its font's new atlas, where its glyphs
        /// now are**: texts in two fonts, drawn once and then fitted to a raster of 3, draw two
        /// batches, one against each font's atlas as it now is and neither against one the fit
        /// replaced, and the first letter's corners are the letter's place in the new atlas. A
        /// replaced atlas's address may be its replacement's, so the corners are what tell a text
        /// that was refitted from one that was not.
        TEST(FontUsersTest, afterAFitEveryTextDrawsAgainstItsFontsNewAtlas)
        {
            Rtx::Testing::GuiInstance gui;
            makeText("DefaultFont", 10);
            makeText("MonoFont", 50);
            gui.draw();
            const std::set<std::uint32_t> replaced{ atlasOf("DefaultFont"), atlasOf("MonoFont") };

            // To 3 and not 2: a default font's atlas twice the side with every glyph twice the size
            // puts each glyph at the same fraction of it, and would draw old corners as new ones.
            gui.getFonts().fit(FontFit{
                .mRaster = 3.0f, .mView = MyGUI::IntSize(341, 256), .mFontSize = gui.getFonts().getFontSize() });

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
        /// them can miss a refit. A skin with a child of the game's own widget types is left out,
        /// which `components-tests` cannot make.
        TEST(FontUsersTest, everyTextAShippedSkinMakesIsTheRegistrys)
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

        /// **A widget sized to its text sizes itself again after a fit**: a label that asks for its
        /// text's size has it before the fit and after it, and the fit moved it, since hinting at
        /// three times the raster lays the letters out a pixel apart where they were not; and after
        /// a fit to 18 points it stands a line of 18 high.
        TEST(FontUsersTest, aWidgetSizedToItsTextSizesItselfAgainAfterAFit)
        {
            Rtx::Testing::GuiInstance gui;
            // In a parent, as every label stands: a widget with none is not sized to its text.
            MyGUI::Widget* const row = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
                "", MyGUI::IntCoord(0, 0, 600, 40), MyGUI::Align::Default, "Windows");
            auto* const label = row->createWidget<AutoSizedTextBox>(
                "SandText", MyGUI::IntCoord(10, 10, 10, 10), MyGUI::Align::Default);
            label->setCaption("Mournhold, Vivec and the Ghostfence");
            const MyGUI::IntSize before = label->getSize();
            ASSERT_EQ(before, label->getRequestedSize());

            gui.getFonts().fit(FontFit{
                .mRaster = 3.0f, .mView = MyGUI::IntSize(341, 256), .mFontSize = gui.getFonts().getFontSize() });

            EXPECT_EQ(label->getSize(), label->getRequestedSize());
            EXPECT_NE(label->getSize(), before) << "the fit moved no letter, so the label proves nothing";

            ASSERT_EQ(label->getHeight(), 16);
            FontFit larger = gui.getFonts().getFit();
            larger.mFontSize = 18;
            gui.getFonts().fit(larger);

            EXPECT_EQ(label->getSize(), label->getRequestedSize());
            EXPECT_EQ(label->getHeight(), 18);
        }

        /// **A text's height is its own or the fonts' size, and never the raster.** A text box given
        /// a height of 20 keeps it, and one given none takes the size the fonts are set at, 16,
        /// before and after a fit to a raster of 2 and in an interface loaded at 2, where MyGUI's
        /// would take the font's own height, 32 raster pixels there. A fit to 18 points has the
        /// second take 18, and the first keep its 20.
        TEST(FontUsersTest, aTextsHeightIsItsOwnOrTheFontsSizeAndNeverTheRaster)
        {
            {
                Rtx::Testing::GuiInstance gui({ .mFontSize = 16 });
                MyGUI::TextBox* const sized = makeText("DefaultFont", 10);
                MyGUI::TextBox* const unsized = makeText("DefaultFont", 50);
                sized->setFontHeight(20);
                ASSERT_EQ(sized->getTextSize().height, 20);
                ASSERT_EQ(unsized->getTextSize().height, 16);

                gui.getFonts().fit(FontFit{ .mRaster = 2.0f, .mView = MyGUI::IntSize(512, 384), .mFontSize = 16 });

                EXPECT_EQ(MyGUI::FontManager::getInstance().getByName("DefaultFont")->getDefaultHeight(), 32);
                EXPECT_EQ(sized->getTextSize().height, 20);
                EXPECT_EQ(unsized->getTextSize().height, 16) << "after a fit to 2";

                gui.getFonts().fit(FontFit{ .mRaster = 2.0f, .mView = MyGUI::IntSize(512, 384), .mFontSize = 18 });

                EXPECT_EQ(sized->getTextSize().height, 20) << "after a fit to 18 points";
                EXPECT_EQ(unsized->getTextSize().height, 18) << "after a fit to 18 points";
            }

            Rtx::Testing::GuiInstance gui({ .mScale = 2.0f, .mFontSize = 16 });
            MyGUI::TextBox* const unsized = makeText("DefaultFont", 10);
            EXPECT_EQ(MyGUI::FontManager::getInstance().getByName("DefaultFont")->getDefaultHeight(), 32);
            EXPECT_EQ(unsized->getTextSize().height, 16) << "in an interface loaded at 2";
        }

        /// **The game's text widgets give their text no height of their own**: a text box and a
        /// button, made under MyGUI's names, are the fork's (`Gui::TextBox`, `Gui::Button`), and
        /// each draws its text at the fonts' size, 16, and at 18 after a fit to it. A height they
        /// stated as their font was set would keep its 16.
        TEST(FontUsersTest, theGamesTextWidgetsGiveTheirTextNoHeightOfTheirOwn)
        {
            Rtx::Testing::GuiInstance gui({ .mFontSize = 16 });
            MyGUI::TextBox* const box = makeText("DefaultFont", 10);
            auto* const button = MyGUI::Gui::getInstance().createWidget<MyGUI::Button>(
                "SandText", MyGUI::IntCoord(10, 50, 300, 30), MyGUI::Align::Default, "Windows");
            button->setFontName("DefaultFont");
            button->setCaption("Abc");
            ASSERT_NE(box->castType<TextBox>(false), nullptr);
            ASSERT_NE(button->castType<Button>(false), nullptr);
            ASSERT_EQ(box->getTextSize().height, 16);
            ASSERT_EQ(button->getTextSize().height, 16);

            FontFit larger = gui.getFonts().getFit();
            larger.mFontSize = 18;
            gui.getFonts().fit(larger);

            EXPECT_EQ(box->getTextSize().height, 18);
            EXPECT_EQ(button->getTextSize().height, 18);
        }
    }
}
