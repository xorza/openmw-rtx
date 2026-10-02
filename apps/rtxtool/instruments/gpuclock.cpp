#include "gpuclock.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace RtxTool
{
    std::string describeThrottle(std::uint64_t mask)
    {
        // NVML's `nvmlClocksEventReason*` bits, in its own order. Idle is among them because a
        // reading taken from an idle card is a reading of the wrong thing, and saying so is the
        // whole point of quoting the clock at all.
        static constexpr std::array<std::pair<std::uint64_t, std::string_view>, 11> sReasons{ {
            { 0x0000000000000001ull, "gpu idle" },
            { 0x0000000000000002ull, "applications clocks setting" },
            { 0x0000000000000004ull, "sw power cap" },
            { 0x0000000000000008ull, "hw slowdown" },
            { 0x0000000000000010ull, "sync boost" },
            { 0x0000000000000020ull, "sw thermal slowdown" },
            { 0x0000000000000040ull, "hw thermal slowdown" },
            { 0x0000000000000080ull, "hw power brake" },
            { 0x0000000000000100ull, "display clock setting" },
            { 0x0000000000000200ull, "board limit" },
            { 0x0000000000000400ull, "reliability" },
        } };

        std::string named;
        for (const auto& [bit, name] : sReasons)
        {
            if ((mask & bit) == 0)
                continue;

            if (!named.empty())
                named += ", ";

            named += name;
        }

        // A bit this does not know is still worth saying: a driver that grew a reason should read as
        // an unknown one rather than as a card nothing is holding back.
        std::uint64_t known = 0;
        for (const auto& [bit, name] : sReasons)
            known |= bit;

        if ((mask & ~known) != 0)
        {
            if (!named.empty())
                named += ", ";

            named += std::format("unknown reason {:#018x}", mask & ~known);
        }

        return named;
    }

    void ClockRange::add(const ClockRange& other)
    {
        if (other.mReadings == 0)
            return;

        if (mReadings == 0)
        {
            *this = other;
            return;
        }

        mLowestMhz = std::min(mLowestMhz, other.mLowestMhz);
        mHighestMhz = std::max(mHighestMhz, other.mHighestMhz);
        mSumMhz += other.mSumMhz;
        mReadings += other.mReadings;
    }

    void GpuClock::add(const GpuClock& other)
    {
        if (!other.mRead)
            return;

        if (!mRead)
        {
            *this = other;
            return;
        }

        mCore.add(other.mCore);
        mMemory.add(other.mMemory);
        if (other.mTemperatureC.has_value())
            mTemperatureC = std::max(mTemperatureC.value_or(0), *other.mTemperatureC);
        if (other.mThrottleMask.has_value())
            mThrottleMask = mThrottleMask.value_or(0) | *other.mThrottleMask;
    }

    GpuClock GpuClock::reading(const std::uint32_t coreMhz, const std::optional<std::uint32_t> memoryMhz,
        const std::optional<std::uint32_t> temperatureC, const std::optional<std::uint64_t> throttle)
    {
        return GpuClock{
            .mCore = ClockRange::of(coreMhz),
            .mMemory = memoryMhz.has_value() ? ClockRange::of(*memoryMhz) : ClockRange{},
            .mTemperatureC = temperatureC,
            .mThrottleMask = throttle,
            .mRead = true,
        };
    }

    namespace
    {
        /// The ends, where the clock moved between them.
        std::string spreadOf(const ClockRange& range)
        {
            if (range.mLowestMhz == range.mHighestMhz)
                return {};
            return std::format(", {}–{}", range.mLowestMhz, range.mHighestMhz);
        }

        std::string_view plural(std::uint32_t count)
        {
            return count == 1 ? "" : "s";
        }
    }

    std::string describeClock(const GpuClock& clock)
    {
        if (!clock.mRead)
            return {};

        // **The mean first, because it is the number a frame time is read against**, with the ends
        // beside it saying whether the card moved while the frames were drawn. How many readings
        // made them is what tells a range worth reading from two samples that happened to agree.
        // **The count is printed whether or not the clock moved.** A card that held still over
        // twenty-nine readings and one asked once say the same number otherwise, and only the first
        // of them is a reading to hold a frame time against.
        const std::uint32_t readings = clock.getReadings();
        std::string line = std::format("  clock {} MHz core over {} reading{}{}", clock.mCore.getMeanMhz(), readings,
            plural(readings), spreadOf(clock.mCore));

        // The memory's count only where it is not the core's, which is where a reading read the one
        // and not the other.
        const ClockRange& memory = clock.mMemory;
        if (memory.mReadings > 0)
            line += std::format(", {} MHz memory{}{}", memory.getMeanMhz(),
                memory.mReadings == readings
                    ? std::string()
                    : std::format(" over {} reading{}", memory.mReadings, plural(memory.mReadings)),
                spreadOf(memory));
        if (clock.mTemperatureC.has_value())
            line += std::format(", {} °C", *clock.mTemperatureC);

        if (!clock.mThrottleMask.has_value())
            return line + " — what holds it back is not read\n";

        const std::string throttle = describeThrottle(*clock.mThrottleMask);
        return line + " — " + (throttle.empty() ? "nothing holding it back" : throttle) + "\n";
    }
}
