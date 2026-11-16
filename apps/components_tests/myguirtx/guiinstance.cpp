#include <gtest/gtest.h>

#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_IFont.h>
#include <MyGUI_IVertexBuffer.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
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

            auto* const text = MyGUI::Gui::getInstance().createWidget<MyGUI::TextBox>(
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

        /// **A vertex buffer holds no vertices until a draw fills it**: sized for a thousand, it takes nothing from
        /// the heap and says the thousand; locked, it holds them, and the lock is the one allocation.
        TEST(RtxGuiVertexBufferTest, holdsNoVerticesUntilADrawFillsIt)
        {
            Rtx::Testing::GuiInstance gui;
            MyGUI::RenderManager& render = MyGUI::RenderManager::getInstance();
            MyGUI::IVertexBuffer* const buffer = render.createVertexBuffer();

            EXPECT_EQ(Rtx::Testing::allocationsIn([&] { buffer->setVertexCount(1000); }), 0u)
                << "a buffer sized and never drawn took its vertices";
            EXPECT_EQ(buffer->getVertexCount(), 1000u);

            MyGUI::Vertex* vertices = nullptr;
            EXPECT_EQ(Rtx::Testing::allocationsIn([&] { vertices = buffer->lock(); }), 1u);
            ASSERT_NE(vertices, nullptr);
            vertices[999] = MyGUI::Vertex{};
            buffer->unlock();

            render.destroyVertexBuffer(buffer);
        }
    }
}
