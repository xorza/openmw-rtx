#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/variables_map.hpp>

#include <components/files/configurationmanager.hpp>
#include <components/files/conversion.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/migration.hpp>
#include <components/settings/settings.hpp>
#include <components/testing/util.hpp>

#include "registry.hpp"

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

            // Upstream writes a side only where it differs from 800 × 600, so a window of 800 × 480
            // is `resolution y` alone, which is the window's height, and the width stays default.
            CategorySettingValueMap shorter{ { { "Video", "resolution y" }, "480" } };
            migrateUserSettings(shorter);
            EXPECT_EQ(shorter,
                (CategorySettingValueMap{
                    { { "Video", "resolution y" }, "0" },
                    { { "Video", "window height" }, "480" },
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

        /// **A released build's no upscaler moves to the upscaler.** It was a mode, `upscale = off`: 0.4.5 had no
        /// upscaler key at all, and 0.5 one of its own beside it, `auto`. No upscaler is the upscaler's choice
        /// now, so the upscaler becomes `off` and the mode goes, left to its default; a file that kept the
        /// mode would not start, since the mode's spelling list refuses `off`. Any other mode stays as it
        /// was, and so does its upscaler.
        TEST(SettingsMigrationTest, aReleasedBuildsNoUpscalerMovesToTheUpscaler)
        {
            const CategorySetting marker("Video", "resolution sets the frame");
            CategorySettingValueMap first{ { { "RTX", "upscale" }, "off" } };
            migrateUserSettings(first);
            EXPECT_EQ(first, (CategorySettingValueMap{ { { "RTX", "upscaler" }, "off" }, { marker, "true" } }));

            CategorySettingValueMap second{ { { "RTX", "upscale" }, "off" }, { { "RTX", "upscaler" }, "auto" } };
            migrateUserSettings(second);
            EXPECT_EQ(second, (CategorySettingValueMap{ { { "RTX", "upscaler" }, "off" }, { marker, "true" } }));

            CategorySettingValueMap upscaled{ { { "RTX", "upscale" }, "quality" }, { { "RTX", "upscaler" }, "dlss" } };
            CategorySettingValueMap kept = upscaled;
            kept[marker] = "true";
            migrateUserSettings(upscaled);
            EXPECT_EQ(upscaled, kept);
        }

        /// **A settings file ahead of the user's goes through the migration as the user's does**: the harness reads the
        /// player's file as such a layer, ahead of its run's own folder, and a released build's `upscale = off` there
        /// reached the mode's spelling list, which refuses it. Three config dirs, the build's own with the shipped
        /// defaults first, a 0.4.5 file in the second and nothing in the last: the upscaler reads `off`, and the mode
        /// the shipped `balanced`, as the user layer's migration leaves them.
        TEST(SettingsMigrationTest, aLayerAheadOfTheUsersIsMigratedAsTheUsersIs)
        {
            const std::filesystem::path root = TestingOpenMW::currentTestDirPath();
            const std::filesystem::path player = root / "player";
            const std::filesystem::path run = root / "run";
            for (const std::filesystem::path& folder : { player, run })
                std::filesystem::create_directories(folder);
            std::ofstream(player / "settings.cfg") << "[RTX]\nupscale = off\n";

            namespace bpo = boost::program_options;
            bpo::options_description description;
            Files::ConfigurationManager::addCommonOptions(description);
            const std::string playerArg = Files::pathToUnicodeString(player);
            const std::string runArg = Files::pathToUnicodeString(run);
            const std::vector<const char*> arguments{ "test", "--replace=config", "--config", playerArg.c_str(),
                "--config", runArg.c_str() };
            bpo::variables_map variables;
            bpo::store(bpo::command_line_parser(static_cast<std::int32_t>(arguments.size()), arguments.data())
                           .options(description)
                           .run(),
                variables);
            bpo::notify(variables);
            Files::ConfigurationManager config(true);
            config.readConfiguration(variables, description, true);
            ASSERT_EQ(config.getActiveConfigPaths(),
                (std::vector<std::filesystem::path>{ config.getLocalPath(), player, run }));

            // The binary's registry, which a load adds to rather than replaces, made again for the tests after.
            Manager::clear();
            Manager::load(config);
            EXPECT_EQ(Manager::getString("upscaler", "RTX"), "off");
            EXPECT_EQ(Manager::getString("upscale", "RTX"), "balanced") << "the shipped mode, the layer's gone";
            standShippedSettings();
        }
    }
}
