#include <gtest/gtest.h>

#include <components/settings/categories.hpp>
#include <components/settings/migration.hpp>

namespace Settings
{
    namespace
    {
        /// **A file from upstream keeps the window it had.** Upstream's `resolution x/y` was the
        /// window, and the fork reads those keys as the frame: a windowed player who came with 2560
        /// by 1440 got an 800 by 600 window drawing 2560 wide. The values move to the window, and the
        /// frame becomes the window's own. A file the fork wrote, which states a window width, is
        /// left as it is, and so is one that states no resolution at all.
        TEST(SettingsMigrationTest, anUpstreamFileKeepsTheWindowItHad)
        {
            CategorySettingValueMap upstream{
                { { "Video", "resolution x" }, "2560" },
                { { "Video", "resolution y" }, "1440" },
            };
            migrateUserSettings(upstream);
            EXPECT_EQ(upstream,
                (CategorySettingValueMap{
                    { { "Video", "resolution x" }, "0" },
                    { { "Video", "resolution y" }, "0" },
                    { { "Video", "window width" }, "2560" },
                    { { "Video", "window height" }, "1440" },
                }));

            CategorySettingValueMap fork{
                { { "Video", "resolution x" }, "1920" },
                { { "Video", "resolution y" }, "1080" },
                { { "Video", "window width" }, "2560" },
            };
            const CategorySettingValueMap kept = fork;
            migrateUserSettings(fork);
            EXPECT_EQ(fork, kept);

            CategorySettingValueMap none{ { { "Video", "window mode" }, "2" } };
            migrateUserSettings(none);
            EXPECT_EQ(none, (CategorySettingValueMap{ { { "Video", "window mode" }, "2" } }));
        }
    }
}
