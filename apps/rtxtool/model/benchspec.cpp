#include "benchspec.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <format>
#include <stdexcept>

#include "blockfile.hpp"

namespace RtxTool
{
    std::uint32_t BenchSpan::getFrames(const float step) const
    {
        if (mFrames > 0)
            return mFrames;

        if (!(mSeconds > 0.0f))
            return 0;

        // At least one, so a span short enough to round to nothing still measures the frame it
        // asked for rather than silently measuring none. **The quotient in single precision**, as a
        // film's keys are timed (`RtxFilmTest.aKeysTimeClosesASpan`), **and rounded under
        // `sUntilClosed`, refused and not wrapped**: through a `long`, 1e8 seconds came to 1.7e9
        // frames, and past what a Windows `long` holds to one.
        assert(step > 0.0f && "a run whose frames stand for no time");
        const double frames = std::round(static_cast<double>(mSeconds / step));
        if (!(frames < static_cast<double>(sUntilClosed)))
            throw std::range_error(
                std::format("{} seconds at {} seconds a frame is more frames than a run counts", mSeconds, step));
        return std::max(1u, static_cast<std::uint32_t>(frames));
    }

    std::vector<std::string> splitNames(std::string_view text)
    {
        std::vector<std::string> names;

        for (std::size_t at = 0; at <= text.size();)
        {
            const std::size_t comma = std::min(text.find(',', at), text.size());
            const std::string_view name = trimmed(text.substr(at, comma - at));

            if (!name.empty())
                names.emplace_back(name);

            at = comma + 1;
        }

        return names;
    }
}
