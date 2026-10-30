#include "frametimes.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <components/crashcatcher/crashinstall.hpp>
#include <components/files/conversion.hpp>
#include <components/platform/fifo.hpp>
#include <components/rtx/renderer/framezone.hpp>

namespace RtxTool
{
    PerfControl::PerfControl(std::filesystem::path fifo, const std::chrono::milliseconds readerWait)
        : mFifo(std::move(fifo))
        , mReaderWait(readerWait)
    {
    }

    void PerfControl::open()
    {
        if (mFifo.empty() || mHandle != Platform::File::Handle::Invalid)
            return;

        // A wait the run asked for, inside a frame, which the hang watch is told is not a hang:
        // perf's thirty seconds are past the twenty the shipped watch allows a frame.
        const Crash::HangPause waiting;

        const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + mReaderWait;
        while (true)
        {
            try
            {
                mHandle = Platform::Fifo::openForWriting(mFifo);
                return;
            }
            catch (const std::system_error& error)
            {
                // `ENXIO` is "nobody reads it yet"; every other failure is final.
                if (error.code() != std::errc::no_such_device_or_address
                    || std::chrono::steady_clock::now() >= deadline)
                    throw;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    void PerfControl::enable()
    {
        open();
        if (mHandle == Platform::File::Handle::Invalid)
            return;

        send("enable\n");
    }

    void PerfControl::disable()
    {
        if (mHandle == Platform::File::Handle::Invalid)
            return;

        send("disable\n");
    }

    void PerfControl::send(std::string_view command)
    {
        Platform::Fifo::write(mHandle, command.data(), command.size());
    }

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

    Misc::Result<void, std::string> writeFrameTimes(const std::filesystem::path& path, const FrameSamples& samples)
    {
        std::ofstream file(path);
        if (!file)
            return Misc::Err{ "cannot open " + Files::pathToUnicodeString(path) };

        for (const Rtx::Timing timing : Rtx::sTimings.values())
            file << (Rtx::indexOf(timing) == 0 ? "" : " ") << Rtx::sTimings.name(timing);
        file << '\n';

        for (std::uint32_t frame = 0; frame < samples.size(); ++frame)
        {
            for (const Rtx::Timing timing : Rtx::sTimings.values())
                file << (Rtx::indexOf(timing) == 0 ? "" : " ") << std::format("{:.3f}", samples.at(timing)[frame]);
            file << '\n';
        }

        file.close();
        if (!file)
            return Misc::Err{ "cannot write " + Files::pathToUnicodeString(path) };
        return {};
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
    void GpuBreakdown::reserve(const std::uint32_t frames)
    {
        mReserved = frames;
        for (ZoneRow& row : mRows)
        {
            row.mTimes.reserve(frames);
            row.mCounters.reserve(static_cast<std::size_t>(frames) * mCounterNames.size());
        }
        mColumn.reserve(frames);
    }

    void GpuBreakdown::clear()
    {
        for (ZoneRow& row : mRows)
        {
            row.mTimes.clear();
            row.mCounters.clear();
            row.mSeen = 0;
        }

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
            for (ZoneRow& row : mRows)
                row.mCounters.reserve(static_cast<std::size_t>(mReserved) * mCounterNames.size());
        }
        assert((counterNames.empty() || counterNames.size() == mCounterNames.size())
            && "the device read other counters in one run");
        assert(counters.size() == counterNames.size() * spans.size() && "not one reading of each counter a span");

        for (std::size_t spanAt = 0; spanAt < spans.size(); ++spanAt)
        {
            const Rtx::GpuSpan& span = spans[spanAt];
            // The index and not the iterator: adding a row invalidates whatever `find_if` returned,
            // and the row about to be pushed to is the one that name is at.
            const auto at = static_cast<std::size_t>(std::find_if(mRows.begin(), mRows.end(), [&](const ZoneRow& row) {
                return row.mZone == span.mZone;
            }) - mRows.begin());

            if (at == mRows.size())
            {
                mRows.push_back(ZoneRow{ .mZone = span.mZone });

                // **Room for the run taken on the frame the zone first appears.** A row that grows
                // does it inside a frame it is timing, and what a growth costs is a copy of every
                // sample taken so far — landing on one frame of the run and reported as its worst.
                mRows.back().mTimes.reserve(mReserved);
                mRows.back().mCounters.reserve(static_cast<std::size_t>(mReserved) * mCounterNames.size());
            }

            ZoneRow& row = mRows[at];
            const std::span<const double> read = counters.subspan(spanAt * counterNames.size(), counterNames.size());
            row.mCounters.insert(row.mCounters.end(), read.begin(), read.end());

            // **One sample a frame, whatever a frame opened the zone.** A pass recorded in
            // batches — the structure builds are — opens its zone several times over one frame, and
            // a row longer than the run then reported a zone as running on more frames than there
            // were: `tlas 0.24 on 620 of 601`. What a frame's budget is spent on is what the frame
            // spent there, so the spans of one frame are that frame's sample.
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
        mZones.reserve(mRows.size());

        for (ZoneRow& row : mRows)
        {
            // A zone an earlier stop met and this one did not.
            if (row.mTimes.empty())
                continue;

            const double spent = std::accumulate(row.mTimes.begin(), row.mTimes.end(), 0.0);

            GpuZone& zone = mZones.emplace_back(GpuZone{
                .mZone = row.mZone,
                .mTimes = summarise(row.mTimes),
                .mFrames = static_cast<std::uint32_t>(row.mTimes.size()),
                .mOfFrames = mFrames,
                .mShareMs = spent / static_cast<double>(mFrames),
            });

            const std::size_t counters = mCounterNames.size();
            for (std::size_t counter = 0; counter < counters && !row.mCounters.empty(); ++counter)
            {
                mColumn.clear();
                for (std::size_t run = counter; run < row.mCounters.size(); run += counters)
                    if (std::isfinite(row.mCounters[run]))
                        mColumn.push_back(row.mCounters[run]);
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

    std::string describeHeadings()
    {
        return std::format(
            "  {:<9}{:>9}{:>10}{:>10}{:>10}{:>10}{:>10}\n", "", "median", "mean", "p95", "p99", "best", "worst");
    }

    std::string describeTimes(std::string_view heading, const FrameTimes& times)
    {
        return std::format("  {:<9}{:9.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}{:10.2f}\n", heading, times.mMedian,
            times.mMean, times.mP95, times.mP99, times.mBest, times.mWorst);
    }

    std::string describeZone(const GpuZone& zone)
    {
        if (zone.isEveryFrame())
            return std::format("{} {:.2f}", Rtx::sFrameZoneNames.name(zone.mZone), zone.mShareMs);

        // What it cost when it ran, and how rarely — the two figures the share is the product of,
        // and without them a pass that stalls a frame every sixty of them reads as a rounding
        // error.
        return std::format("{} {:.2f} ({:.2f} on {} of {})", Rtx::sFrameZoneNames.name(zone.mZone), zone.mShareMs,
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
