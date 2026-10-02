#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace RtxTool
{
    /// One clock over every reading that read it: its range, and the sum and count its mean is.
    struct ClockRange
    {
        std::uint32_t mLowestMhz = 0;
        std::uint32_t mHighestMhz = 0;
        std::uint64_t mSumMhz = 0;
        std::uint32_t mReadings = 0;

        /// Takes `other` in; a range no reading read adds nothing.
        void add(const ClockRange& other);

        /// One reading of `mhz`.
        static ClockRange of(std::uint32_t mhz) { return ClockRange{ mhz, mhz, mhz, 1 }; }

        /// The mean, or nought where no reading read it.
        std::uint32_t getMeanMhz() const { return mReadings > 0 ? static_cast<std::uint32_t>(mSumMhz / mReadings) : 0; }
    };

    /// The card's clock over a place's frames. A frame time without its clock is not a number to
    /// compare: under load this card is held near 1.8 GHz against 2.3 GHz cool, and the same build
    /// measures several per cent apart from one run to the next. A range and not a reading, because
    /// a card moves while a place is measured, and `CardWatch` samples through the frames because
    /// two ends cannot say what the clock did between them.
    ///
    /// **What an instrument cannot read is unknown and not nought**: AMD's sysfs states no reasons a
    /// card is held back, and a missing hwmon file no temperature, and a report that printed nought
    /// for either would claim an unthrottled card at freezing point.
    struct GpuClock
    {
        /// The graphics clock, and the memory's, each kept the same way: the mean is what a frame
        /// time is read against, and a low that one reading of a hundred saw is noise. The memory's
        /// count is its own, because an instrument can read the one and not the other.
        ClockRange mCore;
        ClockRange mMemory;

        /// The highest any reading saw, which is the one that explains a throttle; nothing where no
        /// reading could read one.
        std::optional<std::uint32_t> mTemperatureC;

        /// Why the card was not running faster, as NVML's own bits, or-ed over every reading that
        /// could say; nothing where none could.
        std::optional<std::uint64_t> mThrottleMask;

        /// False where nothing answered — no driver library to ask, another vendor's device — so
        /// such a run reports no clock rather than a made-up one.
        bool mRead = false;

        /// Takes `other` in: the clocks span both, and the reasons are what either saw. A reading
        /// that answered nothing adds nothing.
        void add(const GpuClock& other);

        /// One reading. Named, because a caller that set some of the fields would report a mean of
        /// nought.
        static GpuClock reading(std::uint32_t coreMhz, std::optional<std::uint32_t> memoryMhz,
            std::optional<std::uint32_t> temperatureC, std::optional<std::uint64_t> throttle);

        /// How many readings the clock is over: the core clock's, which every reading reads.
        std::uint32_t getReadings() const { return mCore.mReadings; }
    };

    /// The clock as one line of the report, or empty where nothing answered.
    std::string describeClock(const GpuClock& clock);

    /// What a throttle mask names, in the order NVML's bits are numbered, or empty for a card that
    /// nothing is holding back.
    std::string describeThrottle(std::uint64_t mask);
}
