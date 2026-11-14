#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include <MyGUI_RenderFormat.h>

#include <apps/components_tests/rtx/support/countingrenderer.hpp>
#include <apps/components_tests/rtx/support/death.hpp>
#include <components/myguirtx/texture.hpp>

namespace MyGUIRtx
{
    namespace
    {
        /// **A texture of a size the renderer refused is locked, filled and unlocked, and draws as
        /// nothing**: a font's atlas past the device's side, which MyGUI makes with `createManual`
        /// and fills whole. Each lock hands out the surface's bytes at the format's width, and
        /// nothing is lent or sent; at one channel and at four alike. A texture never made is still
        /// refused at its lock.
        TEST(RtxGuiTextureTest, aRefusedSizeIsFilledAndDrawsNothing)
        {
            Rtx::Testing::CountingRenderer renderer;
            renderer.mGuiSide = 2;

            for (const MyGUI::PixelFormat format : { MyGUI::PixelFormat::L8, MyGUI::PixelFormat::R8G8B8A8 })
            {
                Texture texture("font", renderer, nullptr);
                texture.createManual(4, 3, MyGUI::TextureUsage::Write, format);
                EXPECT_TRUE(texture.getSlot().isNone());

                for (int fill = 0; fill < 2; ++fill)
                {
                    auto* const bytes = static_cast<std::uint8_t*>(texture.lock(MyGUI::TextureUsage::Write));
                    ASSERT_NE(bytes, nullptr);
                    std::memset(bytes, 0xAB, 4 * 3 * texture.getNumElemBytes());
                    texture.unlock();
                }
            }

            EXPECT_EQ(renderer.mGuiAsked, 2u) << "a refused size asked for again";
            EXPECT_TRUE(renderer.mLent.empty()) << "a refused slot was lent";
            EXPECT_EQ(renderer.mGuiSent, 0u) << "a refused slot was sent";

            Texture unmade("unmade", renderer, nullptr);
            Rtx::Testing::expectAssertDies(
                [&] { unmade.lock(MyGUI::TextureUsage::Write); }, "a texture locked before it was created");
            Rtx::Testing::expectAssertDies([&] { unmade.unlock(); }, "a texture unlocked that was not locked");
        }

        /// **A texture of fewer channels is widened to four at its unlock**, each format by its own rule: a
        /// luminance is grey and solid, a luminance with alpha grey under its alpha, and a colour solid. Each lock
        /// hands out a buffer of the format's width, filled afresh and sent widened, as every fill of an atlas is.
        TEST(RtxGuiTextureTest, aNarrowTextureIsWidenedToFourChannelsAtItsUnlock)
        {
            Rtx::Testing::CountingRenderer renderer;
            renderer.mGuiSide = 16;

            const auto widened = [&](const MyGUI::PixelFormat format, const std::vector<std::uint8_t>& texel) {
                Texture texture("narrow", renderer, nullptr);
                texture.createManual(2, 1, MyGUI::TextureUsage::Write, format);
                EXPECT_EQ(texture.getNumElemBytes(), texel.size());
                for (int fill = 0; fill < 2; ++fill)
                {
                    auto* const bytes = static_cast<std::uint8_t*>(texture.lock(MyGUI::TextureUsage::Write));
                    for (std::size_t at = 0; at < 2; ++at)
                        std::memcpy(bytes + at * texel.size(), texel.data(), texel.size());
                    texture.unlock();
                }
                return std::vector<std::uint8_t>(renderer.mLending.begin(), renderer.mLending.end());
            };

            EXPECT_EQ(
                widened(MyGUI::PixelFormat::L8, { 7 }), (std::vector<std::uint8_t>{ 7, 7, 7, 255, 7, 7, 7, 255 }));
            EXPECT_EQ(widened(MyGUI::PixelFormat::L8A8, { 10, 200 }),
                (std::vector<std::uint8_t>{ 10, 10, 10, 200, 10, 10, 10, 200 }));
            EXPECT_EQ(widened(MyGUI::PixelFormat::R8G8B8, { 1, 2, 3 }),
                (std::vector<std::uint8_t>{ 1, 2, 3, 255, 1, 2, 3, 255 }));
            EXPECT_EQ(renderer.mGuiSent, 6u) << "two fills a texture, each sent";
        }
    }
}
