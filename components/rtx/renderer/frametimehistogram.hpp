#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "framespend.hpp"

namespace Rtx
{
    /// **The frame times of a played session, in fixed room however long it runs**: binned at a tenth
    /// of a millisecond up to `sRangeMs`, which every frame a player would call smooth or stuttering
    /// falls inside, with the slowest kept exactly and where its time went. What a player's log says of
    /// how the session ran, which the harness measures exactly over its samples instead.
    class FrameTimeHistogram
    {
    public:
        /// How many bins a millisecond holds, a tenth of one each, which a quantile is read to: a
        /// whole number, so a frame's bin is its time times it, which no division rounds across an
        /// edge.
        static constexpr std::size_t sBinsPerMs = 10;

        /// The longest frame a bin holds; a longer one is counted past the range, and the worst is
        /// kept whatever it was.
        static constexpr std::size_t sRangeMs = 250;

        static constexpr std::size_t sBins = sRangeMs * sBinsPerMs;

        /// What a stretch of frames came to. A quantile is the upper edge of the bin its frame fell
        /// in, by nearest rank, so a frame of 16.66 ms reads as 16.7, and never past the worst; one
        /// past the range reads as the worst.
        struct Summary
        {
            std::uint64_t mFrames = 0;
            double mMedianMs = 0.0;
            double mP99Ms = 0.0;

            /// The slowest frame's spend, and which frame of the stretch it was, from one.
            FrameSpend mWorst;
            std::uint64_t mWorstAt = 0;

            /// Frames longer than twice the median, to a bin: what a player sees as a hitch.
            std::uint64_t mHitches = 0;
        };

        /// Counts a frame by its spend, its `Frame` the time it took (`FrameSpend::closedBy`). Allocates nothing.
        void add(const FrameSpend& frame);

        /// Forgets every frame.
        void clear();

        Summary summarise() const;

    private:
        std::array<std::uint32_t, sBins> mBins{};
        std::uint64_t mPastRange = 0;
        std::uint64_t mFrames = 0;
        FrameSpend mWorst;
        std::uint64_t mWorstAt = 0;
    };

    /// A summary as the log says it: "812 frames, median 16.7 ms, p99 33.4 ms, worst 51.2 ms at frame 40
    /// (update 30.1 place 12.0 walk 4.2), 3 hitches", or "no frames".
    std::string describe(const FrameTimeHistogram::Summary& summary);
}
