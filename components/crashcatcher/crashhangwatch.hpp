#pragma once

#include <chrono>
#include <cstdint>
#include <variant>

namespace Crash
{
    /// What the monitor read of the game on one wake.
    struct HangWake
    {
        std::chrono::steady_clock::time_point mNow;

        /// Frames the game has drawn.
        std::uint64_t mFrames;

        /// How long unchanged is a hang; nought turns the check off.
        std::uint32_t mLimitSeconds;

        /// Something else holds the game still: a debugger, a stop from a shell, or Crashpad reading it.
        bool mHeld;
    };

    /// Nothing to say on this wake.
    struct HangQuiet
    {
        bool operator==(const HangQuiet&) const = default;
    };

    /// The frame counter stood for the limit, said once until it moves again.
    struct HangFound
    {
        std::uint32_t mSeconds;

        /// The frame counter it stopped at.
        std::uint64_t mStalledAt;

        bool operator==(const HangFound&) const = default;
    };

    /// A stall that was found is over.
    struct HangEnded
    {
        std::uint32_t mSeconds;

        bool operator==(const HangEnded&) const = default;
    };

    using HangAnswer = std::variant<HangQuiet, HangFound, HangEnded>;

    /// **What the monitor's watch decides**, a wake at a time, off a stated clock. It begins at the
    /// first frame, so a start that draws nothing for a while is no hang.
    ///
    /// **A stall counts only what lies between two wakes that both found the game stalling**, so it
    /// never reaches the limit sooner than the limit after what began it, and comes about a wake later
    /// at most. The wake that finds a new frame, the limit back on or a game no longer held starts the
    /// count: what lies before it on the clock may be a frame, a watch that was off, or a hold.
    ///
    /// **Each wake adds at most two seconds**: a monitor that slept through a suspend of the machine,
    /// or was not scheduled, wakes once late, and that once is a tick and not the hours it missed. A
    /// held game's stall stops growing and goes on once it runs: the stall the log reports when frames
    /// come again is the game's own.
    class HangWatch
    {
    public:
        explicit HangWatch(std::chrono::steady_clock::time_point start);

        /// Whether a wake that read `frames` starts over, where whether the game is held does not matter.
        bool isNews(std::uint64_t frames) const;

        HangAnswer wake(const HangWake& read);

    private:
        std::chrono::steady_clock::time_point mWoke;
        std::chrono::steady_clock::duration mStalledFor{};
        std::uint64_t mLastFrames = 0;
        bool mStarted = false;
        bool mReported = false;
        bool mCounting = false;
    };
}
