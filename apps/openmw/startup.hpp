#pragma once

#include <string_view>

namespace Files
{
    struct ConfigurationManager;
}

namespace OpenMW
{
    /// Opens the log, loads the settings and hands the crash catcher what it reads of both: the
    /// version every report carries and how long without a frame is a hang. Once the configuration
    /// chain is read, in every binary that runs the engine, so a hang in the harness is reported as
    /// one in the game is.
    void startLogAndSettings(const Files::ConfigurationManager& config, std::string_view application);
}
