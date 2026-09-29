#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Rtx
{
    /// The temporal filters a chain keeps a history for, in the order a frame runs them.
    enum class Temporal : std::uint8_t
    {
        Accumulate,
        Shadow,
        Specular,
        Pane,
    };

    inline constexpr std::size_t sTemporals = static_cast<std::size_t>(Temporal::Pane) + 1;

    /// One flag a temporal filter, indexed by `Temporal`.
    struct TemporalFlags
    {
        std::array<bool, sTemporals> mFlags{};

        bool operator[](const Temporal filter) const { return mFlags[static_cast<std::size_t>(filter)]; }
        bool& operator[](const Temporal filter) { return mFlags[static_cast<std::size_t>(filter)]; }
    };

    /// Which half of every pair a frame reads and which it writes, and which histories hold nothing.
    ///
    /// **One parity for every history of a chain.** A filter that does not run on a frame is fresh
    /// the next time it runs, so which half it then reads does not matter: it reads none.
    ///
    /// No device, so a test reads every answer off it.
    class TemporalTurns
    {
    public:
        TemporalTurns() { reset(); }

        /// What one frame takes of the pairs.
        struct Step
        {
            /// The half the last frame wrote, which this one reads, and the half this one writes.
            std::size_t mBefore = 0;
            std::size_t mNow = 1;

            /// Which filters run this frame, and which of them have no history to read: the first
            /// run after a construction, a `reset`, or a frame the filter did not run on.
            TemporalFlags mRuns;
            TemporalFlags mFresh;
        };

        /// Turns to the other half for the frame being recorded, on which `runs` run.
        Step next(const TemporalFlags& runs)
        {
            const Step step{ .mBefore = mNow, .mNow = 1 - mNow, .mRuns = runs, .mFresh = mFresh };
            mNow = step.mNow;
            for (std::size_t at = 0; at < sTemporals; ++at)
                mFresh.mFlags[at] = !runs.mFlags[at];
            return step;
        }

        /// Says every history is worthless, until each next runs.
        void reset() { mFresh.mFlags.fill(true); }

    private:
        std::size_t mNow = 0;
        TemporalFlags mFresh;
    };
}
