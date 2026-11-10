#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

namespace MWGui
{
    namespace
    {
        std::filesystem::path sourceRoot()
        {
            return std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR };
        }

        /// `path`, under the source root, spelled with `/` in UTF-8: `generic_string` would narrow it
        /// through Windows' code page, which throws for a name outside it.
        std::string nameOf(const std::filesystem::path& path)
        {
            const std::u8string name = path.lexically_relative(sourceRoot()).generic_u8string();
            return std::string(name.begin(), name.end());
        }

        std::string contentsOf(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        }

        /// Whether `source` reads `[GUI] font size`, by its value, `gui().mFontSize` with any space
        /// around the dot, or by its name. A search, not a `std::regex`, which takes most of a
        /// second over the tree in a debug build.
        bool readsFontSize(std::string_view source)
        {
            if (source.find("\"font size\"") != std::string_view::npos)
                return true;

            constexpr std::string_view member = "mFontSize";
            constexpr std::string_view accessor = "gui()";
            for (std::size_t at = source.find(member); at != std::string_view::npos;
                at = source.find(member, at + member.size()))
            {
                std::string_view before = source.substr(0, at);
                const auto trimSpace = [&] {
                    while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back())))
                        before.remove_suffix(1);
                };
                trimSpace();
                if (!before.ends_with('.'))
                    continue;
                before.remove_suffix(1);
                trimSpace();
                if (before.ends_with(accessor))
                    return true;
            }
            return false;
        }

        /// **The window manager's fit is the font size's one reader**: every other size the
        /// interface computes from it asks the fonts' owner (`MWBase::WindowManager::getFontSize`),
        /// so a fit to another size reaches them all. The game and its components read it nowhere
        /// else; the setting's own declaration names it. The scan finds a read by value and one by
        /// name, planted.
        TEST(InterfaceFontSizeTest, theWindowManagersFitIsTheFontSizesOneReader)
        {
            EXPECT_TRUE(readsFontSize("const int h = Settings::gui().mFontSize + 2;"));
            EXPECT_TRUE(readsFontSize("Settings::gui() . mFontSize"));
            EXPECT_TRUE(readsFontSize(R"(Settings::Manager::getInt("font size", "GUI"))"));
            EXPECT_FALSE(readsFontSize("getWindowManager()->getFontSize() + 2"));
            EXPECT_FALSE(readsFontSize("std::string mFontSize;"));

            const std::set<std::string> allowed{ "apps/openmw/mwgui/windowmanagerimp.cpp",
                "components/settings/categories/gui.hpp" };
            std::set<std::string> readers;
            std::size_t scanned = 0;
            for (const char* const tree : { "apps/openmw", "components" })
            {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot() / tree))
                {
                    const std::filesystem::path extension = entry.path().extension();
                    if (!entry.is_regular_file() || (extension != ".cpp" && extension != ".hpp" && extension != ".h"))
                        continue;
                    ++scanned;
                    if (readsFontSize(contentsOf(entry.path())))
                        readers.insert(nameOf(entry.path()));
                }
            }
            ASSERT_GT(scanned, 1000u) << "the scan found next to no source";
            EXPECT_EQ(readers, allowed);
        }

        /// **Every size computed from the font size is known, and follows a fit to another**: the
        /// files that ask the window manager for it are exactly these, each with the way what it
        /// sized is sized again. A new one fails here until it says how it follows.
        TEST(InterfaceFontSizeTest, everySizeComputedFromTheFontSizeFollowsAFit)
        {
            // How each follows. "As it opens": a window that lays its rows out as it opens and is
            // closed whenever the main menu, where the size changes, is open.
            const std::map<std::string, std::string_view> followers{
                { "apps/openmw/mwgui/windowmanagerimp.cpp", "the owner" },
                { "apps/openmw/mwgui/birth.cpp", "BirthDialog::refitText" },
                { "apps/openmw/mwgui/race.cpp", "RaceDialog::refitText" },
                { "apps/openmw/mwgui/review.cpp", "ReviewDialog::refitText" },
                { "apps/openmw/mwgui/statswindow.cpp", "StatsWindow::refitText" },
                { "apps/openmw/mwgui/spellview.cpp", "SpellWindow::refitText" },
                { "apps/openmw/mwgui/settingswindow.cpp", "SettingsWindow::refitText" },
                { "apps/openmw/mwgui/itemwidget.cpp", "InventoryWindow::refitText; every other view as it opens" },
                { "apps/openmw/mwgui/dialogue.cpp", "the history through its page; persuasion as it opens" },
                { "apps/openmw/mwgui/journalbooks.cpp", "the journal's pages" },
                { "apps/openmw/mwgui/formatting.cpp", "a book or a scroll as it opens" },
                { "apps/openmw/mwgui/merchantrepair.cpp", "as it opens; its controller scroll as it scrolls" },
                { "apps/openmw/mwgui/spellbuyingwindow.cpp", "as it opens; its controller scroll as it scrolls" },
                { "apps/openmw/mwgui/spellcreationdialog.cpp", "its controller scroll as it scrolls" },
                { "apps/openmw/mwgui/trainingwindow.cpp", "as it opens" },
                { "apps/openmw/mwgui/travelwindow.cpp", "as it opens; its controller scroll as it scrolls" },
                { "apps/openmw/mwlua/uibindings.cpp", "the scripts, as they ask" },
            };
            std::set<std::string> expected;
            for (const auto& [file, how] : followers)
                expected.insert(file);

            std::set<std::string> askers;
            for (const char* const tree : { "apps/openmw/mwgui", "apps/openmw/mwlua" })
                for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceRoot() / tree))
                    if (entry.path().extension() == ".cpp"
                        && contentsOf(entry.path()).find("getFontSize()") != std::string::npos)
                        askers.insert(nameOf(entry.path()));
            EXPECT_EQ(askers, expected);
        }
    }
}
