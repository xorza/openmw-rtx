#include <gtest/gtest.h>

#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_IFont.h>
#include <MyGUI_TextBox.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/myguirtx/slottexture.hpp>

namespace MyGUIRtx
{
    namespace
    {
        /// **A text draws two triangles a glyph, against its font's texture**: "Abc" in the skin a
        /// window's labels use is one batch of eighteen vertices, and its texture is the default
        /// font's atlas. What every test of the interface's fonts reads.
        TEST(RtxGuiInstanceTest, aTextDrawsTwoTrianglesAGlyphAgainstItsFontsAtlas)
        {
            Rtx::Testing::GuiInstance gui;

            MyGUI::TextBox* const text = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>(
                "SandText", MyGUI::IntCoord(10, 10, 200, 30), MyGUI::Align::Default, "Windows");
            text->setCaption("Abc");

            const Rtx::Testing::DrawnGui drawn = gui.draw();

            MyGUI::IFont* const font = MyGUI::FontManager::getInstance().getByName("DefaultFont");
            ASSERT_NE(font, nullptr) << "the shipped TrueType font did not load";
            const auto* const atlas = dynamic_cast<const SlotTexture*>(font->getTextureFont());
            ASSERT_NE(atlas, nullptr);

            ASSERT_EQ(drawn.mBatches.size(), 1u);
            EXPECT_EQ(drawn.mBatches[0].mTexture, atlas->getSlot());
            EXPECT_EQ(drawn.mBatches[0].mVertexCount, 18u);
            EXPECT_EQ(drawn.mVertices.size(), 18u);
        }
    }
}
