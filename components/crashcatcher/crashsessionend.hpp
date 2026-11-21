#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <components/debug/debuglog.hpp>

namespace Crash
{
    /// What the monitor knows of how the game's session ended, read once the game is gone.
    struct SessionEndFacts
    {
        /// The code the game reached its end with, where it reached it (`SharedPage::getEnd`).
        std::optional<std::int32_t> mReachedEnd;
        /// How the process ended, as the system names it, where it ended without reaching its end and the system
        /// says how.
        std::optional<std::string> mExit;
        /// The reports the session left.
        std::size_t mReports = 0;
        /// Whether the session crashed or a crash of it went unwritten.
        bool mReported = false;
    };

    /// **The monitor's last line on a session, at the level it is**: an end the game reached with nothing to report
    /// is the log's ordinary information, one it reached with a code or reports to say a warning, and an end it never
    /// reached an error.
    struct SessionEnd
    {
        Debug::Level mLevel;
        std::string mText;
    };

    SessionEnd describeSessionEnd(const SessionEndFacts& facts);
}
