#include "frametimes.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <components/files/conversion.hpp>
#include <components/rtx/renderer/framezone.hpp>

#include "reportfile.hpp"

namespace RtxTool
{
    namespace
    {
        /// The `quantile`th value of an already sorted `times`, by nearest rank.
        double rank(const std::vector<double>& times, double quantile)
        {
            const std::size_t place = static_cast<std::size_t>(std::ceil(quantile * static_cast<double>(times.size())));

            // `ceil` of a positive quantile is at least one, and the clamp above catches the other
            // end: a p99 of a run of ten is the tenth of ten rather than the eleventh.
            return times[std::min(times.size(), std::max<std::size_t>(place, 1)) - 1];
        }
    }

    double FrameTimes::getRate() const
    {
        return mMedian > 0.0 ? 1000.0 / mMedian : 0.0;
    }

    double FrameTimes::getLowRate() const
    {
        return mP99 > 0.0 ? 1000.0 / mP99 : 0.0;
    }

    Result<void, std::string> writeFrameTimes(const std::filesystem::path& path, const FrameSamples& samples)
    {
        std::ostringstream file;

        for (const Rtx::Timing timing : Rtx::sTimings.values())
            file << (Rtx::indexOf(timing) == 0 ? "" : " ") << Rtx::sTimings.name(timing);
        file << '\n';

        for (std::uint32_t frame = 0; frame < samples.size(); ++frame)
        {
            for (const Rtx::Timing timing : Rtx::sTimings.values())
                file << (Rtx::indexOf(timing) == 0 ? "" : " ") << std::format("{:.3f}", samples.at(timing)[frame]);
            file << '\n';
        }

        return writeReport(path, file.view());
    }

    FrameTimes summarise(std::vector<double>& times)
    {
        assert(!times.empty() && "a run with no frames in it has no times to summarise");

        std::sort(times.begin(), times.end());

        return FrameTimes{
            .mMean = std::accumulate(times.begin(), times.end(), 0.0) / static_cast<double>(times.size()),
            .mMedian = rank(times, 0.5),
            .mP95 = rank(times, 0.95),
            .mP99 = rank(times, 0.99),
            .mBest = times.front(),
            .mWorst = times.back(),
        };
    }
}

namespace RtxTool
{
    namespace
    {
        static_assert(Rtx::sFrameZoneCount <= std::numeric_limits<std::uint8_t>::max() + std::size_t{ 1 },
            "a zone's row that a run's byte cannot name");

        /// The most runs `frames` frames read counters of: `Rtx::sMaxGpuZones` a frame.
        std::size_t mostRuns(const std::uint32_t frames)
        {
            return std::size_t{ frames } * Rtx::sMaxGpuZones;
        }
    }

    void GpuBreakdown::reserve(const std::uint32_t frames)
    {
        mReserved = frames;
        for (ZoneRow& row : mRows)
            row.mTimes.reserve(frames);
        mZones.reserve(mRows.size());
        reserveReadings();
    }

    void GpuBreakdown::reserveReadings()
    {
        if (mCounterNames.empty())
            return;
        mReadings.reserve(mostRuns(mReserved) * mCounterNames.size());
        mRunRows.reserve(mostRuns(mReserved));
        mColumn.reserve(mostRuns(mReserved));
    }

    void GpuBreakdown::clear()
    {
        for (ZoneRow& row : mRows)
        {
            row.mTimes.clear();
            row.mSeen = 0;
        }
        mReadings.clear();
        mRunRows.clear();

        mFrames = 0;
        mZones.clear();
    }

    void GpuBreakdown::add(std::span<const Rtx::GpuSpan> spans, const std::span<const std::string_view> counterNames,
        const std::span<const double> counters)
    {
        ++mFrames;

        // Taken on the first frame that names any, which is the first measured frame: the device
        // reads the same counters for as long as it stands.
        if (mCounterNames.empty() && !counterNames.empty())
        {
            mCounterNames.assign(counterNames.begin(), counterNames.end());
            reserveReadings();
        }
        assert((counterNames.empty() || counterNames.size() == mCounterNames.size())
            && "the device read other counters in one run");
        assert(counters.size() == counterNames.size() * spans.size() && "not one reading of each counter a span");

        for (std::size_t spanAt = 0; spanAt < spans.size(); ++spanAt)
        {
            const Rtx::GpuSpan& span = spans[spanAt];
            const auto at = static_cast<std::size_t>(span.mZone);
            assert(at < mRows.size());

            if (!counterNames.empty())
            {
                const std::span<const double> read
                    = counters.subspan(spanAt * counterNames.size(), counterNames.size());
                mReadings.insert(mReadings.end(), read.begin(), read.end());
                mRunRows.push_back(static_cast<std::uint8_t>(at));
            }

            ZoneRow& row = mRows[at];

            // One sample a frame whatever a frame opened the zone: a pass recorded in batches opens its zone
            // several times over one frame, and a row longer than the run reported a zone on more frames than
            // there were (`tlas 0.24 on 620 of 601`).
            if (row.mSeen == mFrames)
                row.mTimes.back() += span.mMs;
            else
            {
                row.mTimes.push_back(span.mMs);
                row.mSeen = mFrames;
            }
        }
    }

    std::span<const GpuZone> GpuBreakdown::summariseZones()
    {
        mZones.clear();

        for (std::size_t at = 0; at < mRows.size(); ++at)
        {
            ZoneRow& row = mRows[at];

            // A zone an earlier stop met and this one did not.
            if (row.mTimes.empty())
                continue;

            const double spent = std::accumulate(row.mTimes.begin(), row.mTimes.end(), 0.0);

            GpuZone& zone = mZones.emplace_back(GpuZone{
                .mZone = static_cast<Rtx::FrameZone>(at),
                .mTimes = summarise(row.mTimes),
                .mFrames = static_cast<std::uint32_t>(row.mTimes.size()),
                .mOfFrames = mFrames,
                .mShareMs = spent / static_cast<double>(mFrames),
            });

            const std::size_t counters = mCounterNames.size();
            for (std::size_t counter = 0; counter < counters && !mRunRows.empty(); ++counter)
            {
                mColumn.clear();
                for (std::size_t run = 0; run < mRunRows.size(); ++run)
                    if (const double read = mReadings[run * counters + counter];
                        mRunRows[run] == at && std::isfinite(read))
                        mColumn.push_back(read);
                zone.mCounters[counter]
                    = mColumn.empty() ? std::numeric_limits<double>::quiet_NaN() : summarise(mColumn).mMedian;
            }
        }

        // **By what each cost the run and not by what it cost a frame that ran it**, which is the
        // order "where did the frame go" is asked in: a pass that runs at a cell crossing is
        // seven milliseconds and a fifth of a per-cent of the run, and sorting it to the top of
        // the row put it above the frame median printed over it.
        std::sort(
            mZones.begin(), mZones.end(), [](const GpuZone& a, const GpuZone& b) { return a.mShareMs > b.mShareMs; });

        return mZones;
    }

    std::string describeTimings(const std::array<FrameTimes, Rtx::sTimingCount>& rows)
    {
        constexpr std::string_view sUnit = " ms";
        std::size_t width = 0;
        for (const Rtx::Timing timing : Rtx::sTimings.values())
            width = std::max(width, Rtx::sTimings.name(timing).size() + sUnit.size());

        std::string table = std::format("  {:<{}}{:>9}{:>10}{:>10}{:>10}{:>10}{:>10}\n", "", width + 1, "median",
            "mean", "p95", "p99", "best", "worst");
        for (const Rtx::Timing timing : Rtx::sTimings.values())
        {
            const FrameTimes& times = rows[Rtx::indexOf(timing)];
            table += std::format("  {:<{}}{:9.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}\n",
                std::format("{}{}", Rtx::sTimings.name(timing), sUnit), width + 1, times.mMedian, times.mMean,
                times.mP95, times.mP99, times.mBest, times.mWorst);
        }
        return table;
    }

    std::string describeZone(const GpuZone& zone)
    {
        // **To the microsecond**: a pass of four of them read "0.00" at two places, the same as one that never ran.
        if (zone.isEveryFrame())
            return std::format("{} {:.3f}", Rtx::sFrameZoneNames.name(zone.mZone), zone.mShareMs);

        // What it cost when it ran, and how rarely — the two figures the share is the product of,
        // and without them a pass that stalls a frame every sixty of them reads as a rounding
        // error.
        return std::format("{} {:.3f} ({:.3f} on {} of {})", Rtx::sFrameZoneNames.name(zone.mZone), zone.mShareMs,
            zone.mTimes.mMedian, zone.mFrames, zone.mOfFrames);
    }

    std::string describeZones(std::span<const GpuZone> zones)
    {
        if (zones.empty())
            return {};

        std::string row = "  gpu ms  ";
        for (const GpuZone& zone : zones)
            row += "  " + describeZone(zone);

        return row + "\n";
    }

    namespace
    {
        /// A counter's reading in a column of eleven: a count of waves or instructions runs to
        /// hundreds of millions, which a fixed point would print at eleven digits before its own.
        std::string compactCounter(const double value)
        {
            if (!std::isfinite(value))
                return "-";
            const double size = std::abs(value);
            if (size >= 1e9)
                return std::format("{:.2f}G", value / 1e9);
            if (size >= 1e6)
                return std::format("{:.2f}M", value / 1e6);
            if (size >= 1e4)
                return std::format("{:.2f}k", value / 1e3);
            return std::format("{:.2f}", value);
        }
    }

    std::string describeCounters(const std::span<const GpuZone> zones, const std::span<const std::string> names)
    {
        if (names.empty() || zones.empty())
            return {};

        constexpr std::string_view sHeading = "counters, median a run";
        std::size_t width = sHeading.size();
        for (const std::string& name : names)
            width = std::max(width, name.size());

        const std::span<const GpuZone> shown = zones.first(std::min(zones.size(), sCounterZones));
        std::string table = std::format("  {:<{}}", sHeading, width + 1);
        for (const GpuZone& zone : shown)
            table += std::format("{:>11}", Rtx::sFrameZoneNames.name(zone.mZone));
        table += '\n';

        for (std::size_t counter = 0; counter < names.size(); ++counter)
        {
            table += std::format("  {:<{}}", names[counter], width + 1);
            for (const GpuZone& zone : shown)
                table += std::format("{:>11}", compactCounter(zone.mCounters[counter]));
            table += '\n';
        }
        return table;
    }
}
