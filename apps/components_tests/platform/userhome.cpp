#include <filesystem>
#include <optional>

#include <gtest/gtest.h>

#include <components/platform/userhome.hpp>

namespace
{
    /// **The user's home is a folder that stands**, named whole, on every system the tests run on.
    TEST(PlatformUserHomeTest, theHomeIsAFolderThatStands)
    {
        const std::optional<std::filesystem::path> home = Platform::userHome();
        ASSERT_TRUE(home.has_value());
        EXPECT_TRUE(home->is_absolute()) << *home;
        EXPECT_TRUE(std::filesystem::is_directory(*home)) << *home;
    }
}
