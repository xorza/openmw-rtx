#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <MyGUI_FontData.h>
#include <MyGUI_FontManager.h>
#include <MyGUI_IFont.h>
#include <MyGUI_ITexture.h>
#include <MyGUI_ResourceManager.h>

#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <components/fontloader/interfacefonts.hpp>
#include <components/myguirtx/slottexture.hpp>
#include <components/testing/util.hpp>
#include <components/vfs/pathutil.hpp>

namespace Gui
{
    namespace
    {
        /// Every TrueType font the loader makes of `files/data`: each face, and its book font.
        constexpr std::array<std::string_view, 6> sFonts{ "DefaultFont", "ScrollFont", "MonoFont",
            "Journalbook DefaultFont", "Journalbook ScrollFont", "Journalbook MonoFont" };

        MyGUI::IFont& fontNamed(std::string_view name)
        {
            MyGUI::IFont* const font = MyGUI::FontManager::getInstance().getByName(name);
            EXPECT_NE(font, nullptr) << name;
            return *font;
        }

        Rtx::GuiSlot slotOf(MyGUI::IFont& font)
        {
            return dynamic_cast<const MyGUIRtx::SlotTexture&>(*font.getTextureFont()).getSlot();
        }

        /// What a font's rasterization comes to, as far as anything drawn with it reads.
        struct Raster
        {
            int mHeight = 0;
            int mAtlasWidth = 0;
            int mAtlasHeight = 0;
            MyGUI::GlyphInfo mA;

            bool operator==(const Raster& other) const
            {
                return mHeight == other.mHeight && mAtlasWidth == other.mAtlasWidth
                    && mAtlasHeight == other.mAtlasHeight && mA.width == other.mA.width && mA.height == other.mA.height
                    && mA.advance == other.mA.advance && mA.bearingX == other.mA.bearingX
                    && mA.bearingY == other.mA.bearingY && mA.uvRect == other.mA.uvRect;
            }
        };

        Raster rasterOf(MyGUI::IFont& font)
        {
            return Raster{ .mHeight = font.getDefaultHeight(),
                .mAtlasWidth = font.getTextureFont()->getWidth(),
                .mAtlasHeight = font.getTextureFont()->getHeight(),
                .mA = *font.getGlyphInfo('A') };
        }

        std::array<Raster, sFonts.size()> rastersNow()
        {
            std::array<Raster, sFonts.size()> rasters;
            for (std::size_t at = 0; at < sFonts.size(); ++at)
                rasters[at] = rasterOf(fontNamed(sFonts[at]));
            return rasters;
        }

        /// **A fit rasterizes each font again in place, exactly as a load at the new scale has it.**
        /// An interface loaded at 1 and fitted to 2, over the view a scale of 2 lays a 1024 × 768
        /// frame out at, holds the same font objects, and every font reads as the one a load at 2
        /// makes: its height, its atlas and its letter A. Each font is rasterized anew, into a
        /// texture of its own, at a height the load at 1 did not have. A book font is not: it
        /// follows the book layer's scale into the view times the raster, which is the frame over
        /// the layer, and the frame stayed.
        TEST(FontFitTest, aFitRasterizesEachFontAgainInPlaceAsALoadAtTheNewScaleHasIt)
        {
            std::array<Raster, sFonts.size()> loadedAtTwo;
            {
                const Rtx::Testing::GuiInstance gui({ .mScale = 2.0f });
                loadedAtTwo = rastersNow();
            }

            Rtx::Testing::GuiInstance gui({ .mScale = 1.0f });
            std::array<MyGUI::IFont*, sFonts.size()> fonts{};
            std::array<Rtx::GuiSlot, sFonts.size()> slots;
            for (std::size_t at = 0; at < sFonts.size(); ++at)
            {
                fonts[at] = &fontNamed(sFonts[at]);
                slots[at] = slotOf(*fonts[at]);
            }
            const std::array<Raster, sFonts.size()> loadedAtOne = rastersNow();

            gui.getFonts().fit(FontFit{
                .mRaster = 2.0f, .mView = MyGUI::IntSize(512, 384), .mFontSize = gui.getFonts().getFontSize() });

            const std::array<Raster, sFonts.size()> fitted = rastersNow();
            for (std::size_t at = 0; at < sFonts.size(); ++at)
            {
                const bool book = sFonts[at].starts_with("Journalbook");
                EXPECT_EQ(&fontNamed(sFonts[at]), fonts[at]) << sFonts[at] << " is another object";
                EXPECT_TRUE(fitted[at] == loadedAtTwo[at]) << sFonts[at] << " is not what a load at 2 makes";
                EXPECT_EQ(slotOf(*fonts[at]) != slots[at], !book) << sFonts[at];
                EXPECT_EQ(loadedAtOne[at].mHeight != loadedAtTwo[at].mHeight, !book) << sFonts[at];
            }
        }

        /// **A fit to another font size sets each font at it in place, as a load at that size has
        /// it**: an interface loaded at 16 points and fitted to 18 over the same view holds the same
        /// font objects, each rasterized anew into a texture of its own, and every font reads as the
        /// one a load at 18 makes, the book fonts included, whose resolution stays where their size
        /// moves. A load at 16 and one at 18 differ in every font's height.
        TEST(FontFitTest, aFitToAnotherFontSizeSetsEachFontAtItAsALoadAtThatSizeHasIt)
        {
            std::array<Raster, sFonts.size()> loadedAtEighteen;
            {
                const Rtx::Testing::GuiInstance gui({ .mFontSize = 18 });
                loadedAtEighteen = rastersNow();
            }

            Rtx::Testing::GuiInstance gui({ .mFontSize = 16 });
            std::array<MyGUI::IFont*, sFonts.size()> fonts{};
            std::array<Rtx::GuiSlot, sFonts.size()> slots;
            for (std::size_t at = 0; at < sFonts.size(); ++at)
            {
                fonts[at] = &fontNamed(sFonts[at]);
                slots[at] = slotOf(*fonts[at]);
            }
            const std::array<Raster, sFonts.size()> loadedAtSixteen = rastersNow();

            FontFit fit = gui.getFonts().getFit();
            fit.mFontSize = 18;
            gui.getFonts().fit(fit);

            EXPECT_EQ(gui.getFonts().getFontSize(), 18);
            const std::array<Raster, sFonts.size()> fitted = rastersNow();
            for (std::size_t at = 0; at < sFonts.size(); ++at)
            {
                EXPECT_EQ(&fontNamed(sFonts[at]), fonts[at]) << sFonts[at] << " is another object";
                EXPECT_TRUE(fitted[at] == loadedAtEighteen[at]) << sFonts[at] << " is not what a load at 18 makes";
                EXPECT_NE(slotOf(*fonts[at]), slots[at]) << sFonts[at];
                EXPECT_NE(loadedAtSixteen[at].mHeight, loadedAtEighteen[at].mHeight) << sFonts[at];
            }
        }

        /// **A font is rasterized again only where its resolution moves**: a fit to the fit the fonts
        /// are at makes no texture, and a fit that moves the view alone moves the book fonts alone,
        /// which follow the book layer's scale into the view where a font follows the raster. A view
        /// of a pixel or two puts a book font at the fewest dots an inch that make a pixel an em of
        /// the font size, 72 over 16 rounded up to 5, where MyGUI threw: the two read the same.
        TEST(FontFitTest, aFontIsRasterizedAgainOnlyWhereItsResolutionMoves)
        {
            Rtx::Testing::GuiInstance gui({ .mScale = 1.0f });
            std::array<Rtx::GuiSlot, sFonts.size()> slots;
            for (std::size_t at = 0; at < sFonts.size(); ++at)
                slots[at] = slotOf(fontNamed(sFonts[at]));

            const std::uint32_t asked = gui.getRenderer().mGuiAsked;
            gui.getFonts().fit(gui.getFonts().getFit());
            EXPECT_EQ(gui.getRenderer().mGuiAsked, asked) << "a fit to the same fit made a texture";

            gui.getFonts().fit(FontFit{
                .mRaster = 1.0f, .mView = MyGUI::IntSize(2048, 1536), .mFontSize = gui.getFonts().getFontSize() });
            for (std::size_t at = 0; at < sFonts.size(); ++at)
            {
                const bool book = sFonts[at].starts_with("Journalbook");
                EXPECT_EQ(slotOf(fontNamed(sFonts[at])) != slots[at], book) << sFonts[at];
            }

            gui.getFonts().fit(
                FontFit{ .mRaster = 1.0f, .mView = MyGUI::IntSize(1, 1), .mFontSize = gui.getFonts().getFontSize() });
            const Raster onePixel = rasterOf(fontNamed("Journalbook DefaultFont"));
            gui.getFonts().fit(FontFit{
                .mRaster = 1.0f, .mView = MyGUI::IntSize(1024, 768), .mFontSize = gui.getFonts().getFontSize() });
            gui.getFonts().fit(
                FontFit{ .mRaster = 1.0f, .mView = MyGUI::IntSize(2, 2), .mFontSize = gui.getFonts().getFontSize() });
            EXPECT_TRUE(rasterOf(fontNamed("Journalbook DefaultFont")) == onePixel);
        }

        /// **The glyphs MyGUI makes of no face are its own at the load and after every fit**: the
        /// cursor a bar `ResourceTrueTypeFont::mCursorWidth`, 2 pixels, wide and the font's height
        /// high, the selection's of no width, so a run of it shows no seam, and the tab the 8 pixels
        /// each definition states. A build reads the code points the last build left, these among
        /// them, and would give each the face's box for a glyph it lacks: the console's cursor.
        TEST(FontFitTest, theGlyphsMyGuiMakesOfNoFaceAreItsOwnAtTheLoadAndAfterEveryFit)
        {
            const auto expectOwnGlyphs = [](std::string_view when) {
                for (const std::string_view name : sFonts)
                {
                    MyGUI::IFont& font = fontNamed(name);
                    const MyGUI::GlyphInfo& cursor = *font.getGlyphInfo(MyGUI::FontCodeType::Cursor);
                    EXPECT_EQ(cursor.codePoint, MyGUI::FontCodeType::Cursor) << name << when;
                    EXPECT_EQ(cursor.width, 2.0f) << name << when;
                    EXPECT_EQ(cursor.height, static_cast<float>(font.getDefaultHeight())) << name << when;
                    for (const MyGUI::Char selection :
                        { MyGUI::FontCodeType::Selected, MyGUI::FontCodeType::SelectedBack })
                    {
                        const MyGUI::GlyphInfo& selected = *font.getGlyphInfo(selection);
                        EXPECT_EQ(selected.codePoint, selection) << name << when;
                        EXPECT_EQ(selected.width, 0.0f) << name << when;
                    }
                    const MyGUI::GlyphInfo& tab = *font.getGlyphInfo(MyGUI::FontCodeType::Tab);
                    EXPECT_EQ(tab.codePoint, MyGUI::FontCodeType::Tab) << name << when;
                    EXPECT_EQ(tab.advance, 8.0f) << name << when;
                }
            };

            Rtx::Testing::GuiInstance gui({ .mScale = 1.0f });
            expectOwnGlyphs(" at the load");
            gui.getFonts().fit(FontFit{
                .mRaster = 2.0f, .mView = MyGUI::IntSize(512, 384), .mFontSize = gui.getFonts().getFontSize() });
            expectOwnGlyphs(" after a fit");
        }

        /// **A font whose file is missing is logged and left out, and the load goes on**: what a mod's
        /// font naming a file it does not ship comes to. The default font's definition names a file
        /// no data holds, and the scroll font, whose definition is the shipped one, still loads.
        TEST(FontFitTest, aFontWhoseFileIsMissingIsLoggedAndTheLoadGoesOn)
        {
            TestingOpenMW::VFSTestFile definition(R"(<MyGUI type="Resource" version="1.1">
                <Resource type="ResourceTrueTypeFont">
                    <Property key="Source" value="Missing.ttf"/>
                </Resource>
            </MyGUI>)");

            testing::internal::CaptureStdout();
            {
                const Rtx::Testing::GuiInstance gui(
                    { .mOverrides = { { VFS::Path::Normalized("fonts/mysticcards.omwfont"), &definition } } });
                EXPECT_FALSE(MyGUI::ResourceManager::getInstance().isExist("DefaultFont"));
                EXPECT_TRUE(MyGUI::ResourceManager::getInstance().isExist("ScrollFont"));
            }
            const std::string log = testing::internal::GetCapturedStdout();
            EXPECT_NE(log.find("names fonts/missing.ttf, which is not found"), std::string::npos) << log;
        }

        /// **A fit reads nothing from the VFS**: each font's file is held from its load.
        TEST(FontFitTest, aFitReadsNothingFromTheVfs)
        {
            Rtx::Testing::GuiInstance gui({ .mScale = 1.0f });
            const std::uint32_t reads = gui.getVfsReads();

            gui.getFonts().fit(FontFit{
                .mRaster = 3.0f, .mView = MyGUI::IntSize(341, 256), .mFontSize = gui.getFonts().getFontSize() });

            EXPECT_EQ(gui.getVfsReads(), reads);
            EXPECT_FALSE(slotOf(fontNamed("DefaultFont")).isNone()) << "the font did not rasterize";
        }
    }
}
