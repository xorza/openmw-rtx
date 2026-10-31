#include <cstdint>
#include <memory>
#include <set>

#include <gtest/gtest.h>

#include <MyGUI_FontManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ResourceManager.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/components_tests/rtx/support/guiinstance.hpp>
#include <apps/openmw/mwgui/bookpage.hpp>
#include <components/fontloader/interfacefonts.hpp>
#include <components/myguirtx/slottexture.hpp>

namespace MWGui
{
    namespace
    {
        /// The interface with the book page's widget and skin, as the window manager has them.
        struct BookPageGui
        {
            Rtx::Testing::GuiInstance mGui;

            BookPageGui()
            {
                BookPage::registerMyGUIComponents(mGui.getFonts().getPages());
                MyGUI::ResourceManager::getInstance().load("openmw_journal.skin.xml");
            }

            BookPage* makePage(int left)
            {
                return MyGUI::Gui::getInstance().createWidget<BookPage>(
                    "MW_BookPage", MyGUI::IntCoord(left, 10, 240, 320), MyGUI::Align::Default, "Windows");
            }

            /// Moves the view alone, which rasterizes every book font again and no other.
            void refit()
            {
                mGui.getFonts().fit({ .mRaster = 1.0f,
                    .mView = MyGUI::IntSize(2048, 1536),
                    .mFontSize = mGui.getFonts().getFontSize() });
            }

            /// "Abc", typeset at the size the fonts are set at, as every owner of a page typesets.
            std::shared_ptr<TypesetBook> typeset()
            {
                const std::shared_ptr<BookTypesetter> typesetter
                    = BookTypesetter::create(240, 320, mGui.getFonts().getFontSize());
                typesetter->write(typesetter->createStyle({}, MyGUI::Colour::Black), "Abc");
                return typesetter->complete();
            }

            std::set<std::uint32_t> drawnAgainst()
            {
                std::set<std::uint32_t> slots;
                for (const Rtx::GuiBatch& batch : mGui.draw().mBatches)
                    slots.insert(batch.mTexture.get());
                return slots;
            }
        };

        std::uint32_t bookAtlas()
        {
            return dynamic_cast<const MyGUIRtx::SlotTexture&>(
                *MyGUI::FontManager::getInstance().getByName("Journalbook DefaultFont")->getTextureFont())
                .getSlot()
                .get();
        }

        /// **A fit has a page's owner typeset its book again, and the page draws the new one**: a
        /// page showing a book, refitted, asks its owner once, and draws against the book font's
        /// atlas as the fit left it and not the one it replaced.
        TEST(BookPageTest, aFitHasAPagesOwnerTypesetItsBookAgain)
        {
            BookPageGui gui;
            BookPage* const page = gui.makePage(10);
            int asked = 0;
            page->adviseRetypeset([&] {
                ++asked;
                page->showPage(gui.typeset(), 0);
            });
            page->showPage(gui.typeset(), 0);
            const std::uint32_t replaced = bookAtlas();
            ASSERT_EQ(gui.drawnAgainst(), std::set<std::uint32_t>{ replaced });

            gui.refit();

            EXPECT_EQ(asked, 1);
            EXPECT_NE(bookAtlas(), replaced);
            EXPECT_EQ(gui.drawnAgainst(), std::set<std::uint32_t>{ bookAtlas() });
        }

        /// **An owner of two pages is asked once, and a page with no book not at all**: the owner
        /// shows a book on both of its pages from the first page's ask, which leaves the second
        /// nothing to ask for; a third page, empty, is not refitted into a book.
        TEST(BookPageTest, anOwnerOfTwoPagesIsAskedOnceAndAnEmptyPageNotAtAll)
        {
            BookPageGui gui;
            BookPage* const left = gui.makePage(10);
            BookPage* const right = gui.makePage(260);
            BookPage* const empty = gui.makePage(510);
            int asked = 0;
            const auto showBoth = [&] {
                const std::shared_ptr<TypesetBook> book = gui.typeset();
                left->showPage(book, 0);
                right->showPage(book, 1);
            };
            left->adviseRetypeset([&] {
                ++asked;
                showBoth();
            });
            right->adviseRetypeset([&] {
                ++asked;
                showBoth();
            });
            int askedEmpty = 0;
            empty->adviseRetypeset([&] { ++askedEmpty; });
            showBoth();

            gui.refit();

            EXPECT_EQ(asked, 1);
            EXPECT_EQ(askedEmpty, 0);
        }

        /// **A book shown on a page with no way to typeset it again asserts**: a fit would leave the
        /// page empty.
        TEST(BookPageTest, aBookShownOnAPageWithNoWayToTypesetItAgainAsserts)
        {
            Rtx::Testing::expectAssertDies(
                [] {
                    BookPageGui gui;
                    gui.makePage(10)->showPage(gui.typeset(), 0);
                },
                "a book shown on a page with no way to typeset it again");
        }
    }
}
