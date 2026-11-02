#include "crashanswering.hpp"

#include <charconv>
#include <system_error>

namespace Crash
{
    namespace
    {
        constexpr std::string_view sAfterAsk = "end-after-ask-";
        constexpr std::string_view sAfterReport = "end-after-report-";
    }

    std::string EndAfter::spell() const
    {
        return std::string(mFrom == From::Ask ? sAfterAsk : sAfterReport) + std::to_string(mDelay.count());
    }

    std::optional<EndAfter> EndAfter::read(std::string_view spelled)
    {
        From from = From::Ask;
        if (spelled.starts_with(sAfterAsk))
            spelled.remove_prefix(sAfterAsk.size());
        else if (spelled.starts_with(sAfterReport))
        {
            spelled.remove_prefix(sAfterReport.size());
            from = From::Report;
        }
        else
            return std::nullopt;

        // A delay that does not read as a number to its end is no delay, and not the one its first
        // digits make.
        std::chrono::milliseconds::rep delay = 0;
        const std::from_chars_result parsed = std::from_chars(spelled.data(), spelled.data() + spelled.size(), delay);
        if (parsed.ec != std::errc() || parsed.ptr != spelled.data() + spelled.size() || delay < 0)
            return std::nullopt;
        return EndAfter{ std::chrono::milliseconds(delay), from };
    }
}
