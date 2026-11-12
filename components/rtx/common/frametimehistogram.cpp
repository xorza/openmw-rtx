#include "frametimehistogram.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace Rtx
{
    void FrameTimeHistogram::add(const double ms)
    {
        ++mFrames;
        mWorstMs = std::max(mWorstMs, ms);
        const double bin = std::floor(std::max(ms, 0.0) * static_cast<double>(sBinsPerMs));
        if (bin >= static_cast<double>(sBins))
            ++mPastRange;
        else
            ++mBins[static_cast<std::size_t>(bin)];
    }

    void FrameTimeHistogram::clear()
    {
        mBins.fill(0);
        mPastRange = 0;
        mFrames = 0;
        mWorstMs = 0.0;
    }

    FrameTimeHistogram::Summary FrameTimeHistogram::summarise() const
    {
        if (mFrames == 0)
            return Summary{};

        // The bin the `quantile`th frame by nearest rank fell in, as its upper edge, or the worst
        // where it fell past the range.
        const auto rank = [&](const double quantile) {
            const auto place = std::max<std::uint64_t>(
                static_cast<std::uint64_t>(std::ceil(quantile * static_cast<double>(mFrames))), 1);
            std::uint64_t counted = 0;
            for (std::size_t bin = 0; bin < sBins; ++bin)
            {
                counted += mBins[bin];
                // Never past the worst, which a bin's upper edge is where the worst fell in it.
                if (counted >= place)
                    return std::min(static_cast<double>(bin + 1) / static_cast<double>(sBinsPerMs), mWorstMs);
            }
            return mWorstMs;
        };

        const double median = rank(0.5);

        // Bins whose every frame is longer than twice the median, and everything past the range.
        std::uint64_t hitches = mPastRange;
        for (std::size_t bin = 0; bin < sBins; ++bin)
            if (static_cast<double>(bin) / static_cast<double>(sBinsPerMs) > 2.0 * median)
                hitches += mBins[bin];

        return Summary{
            .mFrames = mFrames, .mMedianMs = median, .mP99Ms = rank(0.99), .mWorstMs = mWorstMs, .mHitches = hitches
        };
    }

    std::string describe(const FrameTimeHistogram::Summary& summary)
    {
        if (summary.mFrames == 0)
            return "no frames";
        return std::format("{} {}, median {:.1f} ms, p99 {:.1f} ms, worst {:.1f} ms, {} {}", summary.mFrames,
            summary.mFrames == 1 ? "frame" : "frames", summary.mMedianMs, summary.mP99Ms, summary.mWorstMs,
            summary.mHitches, summary.mHitches == 1 ? "hitch" : "hitches");
    }
}
