#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/folder.hpp>

namespace
{
    /// **A folder is listed whole, and a folder that cannot be listed is nothing**: two files and a
    /// folder come back, each once, and the listing can be acted on, every entry removed, without an
    /// iterator to keep step with. A path that is no folder lists nothing rather than throwing.
    TEST(PlatformFolderTest, aFolderIsListedWholeAndAMissingOneIsNothing)
    {
        const std::filesystem::path folder = std::filesystem::temp_directory_path()
            / ("openmw-folder-" + std::to_string(::testing::UnitTest::GetInstance()->random_seed()));
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder / "inner");
        std::ofstream(folder / "one") << "1";
        std::ofstream(folder / "two") << "2";

        const std::optional<std::vector<std::filesystem::directory_entry>> listed = Platform::listFolder(folder);
        ASSERT_TRUE(listed.has_value());
        std::vector<std::string> names;
        for (const std::filesystem::directory_entry& entry : *listed)
            names.push_back(entry.path().filename().string());
        std::ranges::sort(names);
        EXPECT_EQ(names, (std::vector<std::string>{ "inner", "one", "two" }));

        for (const std::filesystem::directory_entry& entry : *listed)
            std::filesystem::remove_all(entry.path());
        EXPECT_EQ(Platform::listFolder(folder)->size(), 0u);

        std::filesystem::remove(folder);
        EXPECT_FALSE(Platform::listFolder(folder).has_value());
    }
}
