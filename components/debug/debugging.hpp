#ifndef DEBUG_DEBUGGING_H
#define DEBUG_DEBUGGING_H

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
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

#ifdef _WIN32
    bool attachParentConsole();
#endif

    // Room for the stamp a log line starts with
    constexpr std::size_t sStampCapacity = 32;

    // Writes the stamp a log line starts with, "[13:04:05.123 E] ", for `level` at `now` into `into`,
    // and returns how much it wrote
    std::size_t writeStamp(
        std::span<char, sStampCapacity> into, Level level, std::chrono::system_clock::time_point now);

    using LogListener = std::function<void(Debug::Level, std::string_view prefix, std::string_view msg)>;
    void setLogListener(LogListener);

    // Can be used to print messages without timestamps
    std::ostream& getRawStdout();

    std::ostream& getRawStderr();

    Misc::Locked<std::ostream&> getLockedRawStderr();

    Level getDebugLevel();

    Level getRecastMaxLogLevel();

    // Redirect cout and cerr to the log file
    void setupLogging(const std::filesystem::path& logDir, std::string_view appName);

    /// Hands the crash catcher the folder its packages go to, `crashes/` in the user data folder the
    /// configuration names, where `OPENMW_CRASH_REPORTS` named none: the catcher started before any
    /// configuration was read, and saves and logs go where the configuration says.
    void setCrashReports(const std::filesystem::path& userData);

    int wrapApplication(
        int (*innerApplication)(int argc, char* argv[]), int argc, char* argv[], std::string_view appName);
}

#endif
