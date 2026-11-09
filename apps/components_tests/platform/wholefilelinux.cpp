#include <filesystem>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <components/platform/wholefile.hpp>
#include <components/testing/util.hpp>

namespace
{
    /// **A file read to its end, whatever size the system states**: `/proc` states nought for every
    /// file, and a reader that read the stated size read nothing. The process's status holds its
    /// name first and its context switches last; its memory map runs past the first read's page, a
    /// line a mapping, each ended.
    TEST(PlatformWholeFileLinuxTest, aFileIsReadToItsEndWhateverSizeItStates)
    {
        std::string status;
        ASSERT_TRUE(Platform::readWhole("/proc/self/status", status).isOk());
        EXPECT_TRUE(status.starts_with("Name:\t")) << status.substr(0, 40);
        EXPECT_NE(status.find("\nnonvoluntary_ctxt_switches:\t"), std::string::npos)
            << "the read stopped short of the last field";

        std::string maps;
        ASSERT_TRUE(Platform::readWhole("/proc/self/smaps", maps).isOk());
        EXPECT_GT(maps.size(), 4096u) << "the read stopped at the first page";
        EXPECT_TRUE(maps.ends_with('\n')) << "the read stopped inside a line";
    }

    /// **A write through a link writes the file it names and keeps the link**, as a write in place
    /// did: a settings file linked from a dotfiles folder stays linked, and the folder's file holds
    /// the new text. A link that names nothing is written over, as there is no file to keep.
    TEST(PlatformWholeFileLinuxTest, aWriteThroughALinkWritesTheFileItNames)
    {
        const std::filesystem::path folder = TestingOpenMW::currentTestDirPath();
        const std::filesystem::path kept = folder / "dotfiles.cfg";
        const std::filesystem::path link = folder / "settings.cfg";
        ASSERT_TRUE(Platform::writeWhole(kept, std::string_view("old"), Platform::Durability::Whole).isOk());
        std::filesystem::create_symlink(kept, link);

        ASSERT_TRUE(Platform::writeWhole(link, std::string_view("new"), Platform::Durability::Synced).isOk());
        EXPECT_TRUE(std::filesystem::is_symlink(link)) << "the link was replaced by a file";
        std::string text;
        ASSERT_TRUE(Platform::readWhole(kept, text).isOk());
        EXPECT_EQ(text, "new");

        const std::filesystem::path dangling = folder / "dangling.cfg";
        std::filesystem::create_symlink(folder / "nowhere.cfg", dangling);
        ASSERT_TRUE(Platform::writeWhole(dangling, std::string_view("made"), Platform::Durability::Whole).isOk());
        EXPECT_FALSE(std::filesystem::is_symlink(dangling));
        ASSERT_TRUE(Platform::readWhole(dangling, text).isOk());
        EXPECT_EQ(text, "made");
    }
}
