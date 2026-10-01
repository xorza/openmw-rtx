#include <components/debug/debugging.hpp>
#include <components/misc/strings/conversion.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/values.hpp>
#include <components/testing/util.hpp>

#include <gtest/gtest.h>

#include <filesystem>

int main(int argc, char** argv)
{
    Log::sMinDebugLevel = Debug::getDebugLevel();

    const std::filesystem::path settingsDefaultPath = std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files"
        / Misc::StringUtils::stringToU8String("settings-default.cfg");

    Settings::SettingsFileParser parser;
    parser.loadSettingsFile(settingsDefaultPath, Settings::Manager::mDefaultSettings);

    Settings::StaticValues::initDefaults();

    Settings::Manager::mUserSettings = Settings::Manager::mDefaultSettings;

    Settings::StaticValues::init();

    // **Death tests run the binary again rather than fork it.** A fork copies the one thread that
    // forked, so a lock another thread held stays held in the child: gtest warns of it at every death
    // test of a process with threads, and ThreadSanitizer refuses such a child outright. Before the
    // command line is read, so `--gtest_death_test_style` still chooses.
    GTEST_FLAG_SET(death_test_style, "threadsafe");

    testing::InitGoogleTest(&argc, argv);
    testing::UnitTest::GetInstance()->listeners().Append(new TestingOpenMW::FreshTestDirs);

    const int result = RUN_ALL_TESTS();
    if (result == 0)
        std::filesystem::remove_all(TestingOpenMW::outputDir());
    return result;
}
