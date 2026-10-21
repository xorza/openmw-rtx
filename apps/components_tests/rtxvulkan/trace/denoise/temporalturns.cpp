#include <array>
#include <cstddef>

#include <gtest/gtest.h>

#include <components/rtxvulkan/trace/denoise/temporalturns.hpp>

namespace Rtx
{
    namespace
    {
        TemporalFlags flags(
            const bool accumulate, const bool sky, const bool lamps, const bool specular, const bool pane)
        {
            return TemporalFlags{ { accumulate, sky, lamps, specular, pane } };
        }

        struct Frame
        {
            bool mResetBefore;
            TemporalFlags mRuns;
            std::size_t mBefore;
            std::size_t mNow;
            TemporalFlags mFresh;
            std::uint32_t mNumber;
            std::uint32_t mWrittenBefore;
        };

        /// **Five frames by hand.** The first reads nothing. The second: the glossy filter did not run
        /// on the first, so it is fresh, and the sky's shadow, which ran, is not — whether it runs
        /// now does not change what it would read. The third: the sky's shadow skipped the second,
        /// so it is fresh again, and the lamps', which ran on the second alone, are not. The fourth
        /// follows a reset and reads nothing, on the half parity gives it and not the half it began
        /// on. The fifth reads everything. Each says which frame wrote what it reads: the one turned
        /// before it, whatever its number — 13 is skipped and 15 held — and the first its own.
        TEST(RtxTemporalTurnsTest, parityAlternatesAndAFilterIsFreshAfterAFrameItDidNotRunOrAReset)
        {
            const std::array frames{
                Frame{
                    false, flags(true, true, false, false, true), 0, 1, flags(true, true, true, true, true), 12, 12 },
                Frame{
                    false, flags(true, false, true, true, true), 1, 0, flags(false, false, true, true, false), 14, 12 },
                Frame{
                    false, flags(true, true, true, true, true), 0, 1, flags(false, true, false, false, false), 15, 14 },
                Frame{ true, flags(true, true, true, true, true), 1, 0, flags(true, true, true, true, true), 15, 15 },
                Frame{ false, flags(true, true, true, true, true), 0, 1, flags(false, false, false, false, false), 16,
                    15 },
            };

            TemporalTurns turns;
            for (std::size_t at = 0; at < frames.size(); ++at)
            {
                const Frame& frame = frames[at];
                if (frame.mResetBefore)
                    turns.reset();

                const TemporalTurns::Step step = turns.next(frame.mRuns, frame.mNumber);
                EXPECT_EQ(step.mBefore, frame.mBefore) << "frame " << at;
                EXPECT_EQ(step.mNow, frame.mNow) << "frame " << at;
                EXPECT_EQ(step.mRuns.mFlags, frame.mRuns.mFlags) << "frame " << at;
                EXPECT_EQ(step.mFresh.mFlags, frame.mFresh.mFlags) << "frame " << at;
                EXPECT_EQ(step.mWrittenBefore, frame.mWrittenBefore) << "frame " << at;
            }
        }
    }
}
