#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <components/rtx/renderer/framespend.hpp>

namespace RtxTool
{
    /// **A run's longest frames, each with when it ended on the monotonic clock**: `CLOCK_MONOTONIC`, which
    /// `std::chrono::steady_clock` reads on Linux and `perf record -k CLOCK_MONOTONIC` stamps its samples by, so a
    /// profile recorded beside the run is cut to each frame. The `sKept` longest, a frame let in only past the least
    /// of them, so a frame that is not among them builds no string.
    class LongestFrames
    {
    public:
        static constexpr std::size_t sKept = 32;

        /// Where a frame offered was seen: its leg, its count in the leg, and when it ended.
        struct Seen
        {
            std::string_view mLeg;
            std::uint32_t mFrame = 0;
            std::int64_t mEndNs = 0;
        };

        LongestFrames() { mKept.reserve(sKept); }

        /// Keeps the frame `spend` closed, seen as `seen`, where it is among the `sKept` longest so far.
        void offer(const Rtx::FrameSpend& spend, const Seen& seen);

        /// The frames kept, longest first, a line each: its time, its leg and count, the second it ended and the
        /// stretches it was made of; nothing where none was offered.
        std::string describe() const;

    private:
        struct Kept
        {
            double mMs = 0.0;
            std::int64_t mEndNs = 0;
            std::uint32_t mFrame = 0;
            std::string mLeg;
            std::string mStretches;
        };

        /// A heap whose front is the shortest kept, which a longer frame takes the place of once `sKept` stand.
        std::vector<Kept> mKept;
    };
}
