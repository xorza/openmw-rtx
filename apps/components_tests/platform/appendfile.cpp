#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include <components/platform/appendfile.hpp>

namespace
{
    std::string contentsOf(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    /// **Every write lands at the end, whoever else wrote there since**: two holders of one file, as
    /// the game and its crash monitor are, write in turn, and nothing is written over. A holder that
    /// wrote at its own offset would have put its second line over the other's first. Emptied where
    /// asked, and kept where not; a path that cannot be opened is a file that writes nothing.
    TEST(PlatformAppendFileTest, everyWriteLandsAtTheEndWhoeverElseWrote)
    {
        const std::filesystem::path path = std::filesystem::temp_directory_path()
            / ("openmw-append-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        {
            std::ofstream old(path, std::ios::binary);
            old << "an old log\n";
        }

        {
            const Platform::AppendFile game = Platform::AppendFile::open(path, true);
            ASSERT_TRUE(game.isOpen());
            EXPECT_EQ(contentsOf(path), "") << "the old log was not emptied";

            const Platform::AppendFile monitor = Platform::AppendFile::open(path, false);
            ASSERT_TRUE(monitor.isOpen());
            game.write("one\n");
            monitor.write("two\n");
            game.write("three\n");
        }
        EXPECT_EQ(contentsOf(path), "one\ntwo\nthree\n");

        {
            const Platform::AppendFile again = Platform::AppendFile::open(path, false);
            again.write("four\n");
        }
        EXPECT_EQ(contentsOf(path), "one\ntwo\nthree\nfour\n") << "a file opened without emptying lost what it held";
        std::filesystem::remove(path);

        const Platform::AppendFile nowhere = Platform::AppendFile::open(
            std::filesystem::temp_directory_path() / "openmw-no-such-folder" / "log", false);
        EXPECT_FALSE(nowhere.isOpen());
        nowhere.write("nothing\n");
    }
}
