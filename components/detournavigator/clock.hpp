#ifndef OPENMW_COMPONENTS_DETOURNAVIGATOR_CLOCK_H
#define OPENMW_COMPONENTS_DETOURNAVIGATOR_CLOCK_H

#include <chrono>

namespace DetourNavigator
{
    /// Where the navmesh updater reads the time from, which decides when a tile changed twice within
    /// `mMinUpdateInterval` is built again: the steady clock, or one a test moves itself, so what a delay
    /// does is asserted at a stated time and not raced against the wall (Chromium's `base::TickClock`).
    class Clock
    {
    public:
        Clock() = default;
        virtual ~Clock() = default;

        Clock(const Clock&) = delete;
        Clock& operator=(const Clock&) = delete;
        Clock(Clock&&) = delete;
        Clock& operator=(Clock&&) = delete;

        virtual std::chrono::steady_clock::time_point now() const = 0;
    };

    class SteadyClock final : public Clock
    {
    public:
        std::chrono::steady_clock::time_point now() const override { return std::chrono::steady_clock::now(); }
    };

    /// The steady clock, which every updater but a test's reads.
    inline const Clock& steadyClock()
    {
        static const SteadyClock clock;
        return clock;
    }
}

#endif
