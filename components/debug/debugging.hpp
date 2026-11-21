#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>

#include <components/misc/guarded.hpp>

#include "debuglog.hpp"

namespace Debug
{
    // ANSI colors for terminal
    enum Color
    {
        Reset = 0,
        DarkGray = 90,
        Red = 91,
        Yellow = 93
    };

    using LogListener = std::function<void(Debug::Level, std::string_view prefix, std::string_view msg)>;
    void setLogListener(LogListener);

    // Can be used to print messages without timestamps
    std::ostream& getRawStdout();

    std::ostream& getRawStderr();

    Misc::Locked<std::ostream&> getLockedRawStderr();

    Level getDebugLevel();

    Level getRecastMaxLogLevel();

    /// Redirects cout and cerr to the log file in `logDir`, and hands the crash catcher that log and `crashes/` in
    /// `userData`, where `OPENMW_CRASH_REPORTS` named no folder: the catcher started before any configuration was
    /// read, and every program that reads one sends its reports where the configuration says, as its saves and logs.
    void setupLogging(
        const std::filesystem::path& logDir, const std::filesystem::path& userData, std::string_view appName);

    std::int32_t wrapApplication(std::int32_t (*innerApplication)(std::int32_t argc, char* argv[]), std::int32_t argc,
        char* argv[], std::string_view appName);
}
