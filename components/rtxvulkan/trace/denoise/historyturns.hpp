#pragma once

#include <cstddef>

namespace Rtx
{
    /// Which half of each pair of a history a frame writes, and whether the history holds anything:
    /// the rule every temporal pass's history keeps. A frame reads the half the last one wrote and
    /// writes the other; the first frame after a `restart` — a new extent — or a `reset` reads
    /// nothing.
    class HistoryTurns
    {
    public:
        /// What one frame takes of the pairs.
        struct Step
        {
            /// The half the last frame wrote, which this one reads.
            std::size_t mBefore;

            /// The half this frame writes.
            std::size_t mNow;

            /// The first frame after a `restart` or a `reset`, whose history is worthless.
            bool mFresh;
        };

        /// Turns to the other half for the frame being recorded.
        Step next()
        {
            const Step step{ .mBefore = mNow, .mNow = 1 - mNow, .mFresh = mFresh };
            mNow = step.mNow;
            mFresh = false;
            return step;
        }

        /// Says the history is worthless, until the next `next`.
        void reset() { mFresh = true; }

        /// Starts again as a new extent does: from the first half, with nothing held.
        void restart() { *this = HistoryTurns{}; }

        /// The half the last `next` handed out to write.
        std::size_t getNow() const { return mNow; }

        bool isFresh() const { return mFresh; }

    private:
        std::size_t mNow = 0;
        bool mFresh = true;
    };
}
