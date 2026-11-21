#pragma once

#include <filesystem>

#include <components/misc/strings/conversion.hpp>
#include <components/settings/parser.hpp>
#include <components/settings/settings.hpp>
#include <components/settings/values.hpp>

namespace Settings
{
    /// **The registry every test of the binary reads**: the shipped defaults, as the user's values too, and the
    /// static values over them. Made by `main` before any test, and again by a test that loads a registry of its own.
    inline void standShippedSettings()
    {
        Manager::clear();
        SettingsFileParser parser;
        parser.loadSettingsFile(std::filesystem::path{ OPENMW_PROJECT_SOURCE_DIR } / "files"
                / Misc::StringUtils::stringToU8String("settings-default.cfg"),
            Manager::mDefaultSettings);
        StaticValues::initDefaults();
        Manager::mUserSettings = Manager::mDefaultSettings;
        StaticValues::init();
    }
}
