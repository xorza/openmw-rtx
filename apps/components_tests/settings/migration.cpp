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
        /// frame becomes the window's own. A file the fork wrote, which states a window width or
        /// carries the fork's marker, is left as it is, and so is one that states no resolution at
        /// all; every file read leaves with the marker, so the next save is a fork file. **A
        /// marked file with a frame and no window**, which a game never windowed writes, keeps
        /// its frame: read as upstream's, it lost it at every start.
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
                    { { "Video", "resolution sets the frame" }, "true" },
                }));

            CategorySettingValueMap marked{
                { { "Video", "resolution x" }, "1920" },
                { { "Video", "resolution y" }, "1080" },
                { { "Video", "resolution sets the frame" }, "true" },
            };
            const CategorySettingValueMap framed = marked;
            migrateUserSettings(marked);
            EXPECT_EQ(marked, framed) << "a fork file's frame was read as upstream's window";

            CategorySettingValueMap fork{
                { { "Video", "resolution x" }, "1920" },
                { { "Video", "resolution y" }, "1080" },
                { { "Video", "window width" }, "2560" },
            };
            CategorySettingValueMap kept = fork;
            kept[{ "Video", "resolution sets the frame" }] = "true";
            migrateUserSettings(fork);
            EXPECT_EQ(fork, kept);

            CategorySettingValueMap none{ { { "Video", "window mode" }, "2" } };
            migrateUserSettings(none);
            EXPECT_EQ(none,
                (CategorySettingValueMap{
                    { { "Video", "window mode" }, "2" }, { { "Video", "resolution sets the frame" }, "true" } }));
        }
    }
}
