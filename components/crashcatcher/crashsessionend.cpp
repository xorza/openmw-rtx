#include "crashsessionend.hpp"

namespace Crash
{
    SessionEnd describeSessionEnd(const SessionEndFacts& facts)
    {
        const std::string left = ", and the session left " + std::to_string(facts.mReports)
            + (facts.mReports == 1 ? " report" : " reports");
        if (facts.mReachedEnd.has_value())
        {
            const bool quiet = *facts.mReachedEnd == 0 && facts.mReports == 0;
            return { .mLevel = quiet ? Debug::Info : Debug::Warning,
                .mText = "The game reached its end with exit code " + std::to_string(*facts.mReachedEnd) + left };
        }
        if (facts.mExit.has_value())
            return { .mLevel = Debug::Error,
                .mText = "The game ended with " + *facts.mExit + " before reaching its end" + left };
        if (facts.mReported)
            return { .mLevel = Debug::Error, .mText = "The game ended before reaching its end" + left };
        // **An end no handler inside the game saw**, which only the missing word says: an out-of-memory kill, or
        // another process's `SIGKILL`.
        return { .mLevel = Debug::Error,
            .mText
            = "The game ended without reaching its end and left no report: the system or another program "
              "ended it" };
    }
}
