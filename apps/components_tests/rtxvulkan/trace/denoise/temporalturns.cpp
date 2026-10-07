#include <array>
#include <cstddef>

#include <gtest/gtest.h>

#include <components/rtxvulkan/trace/denoise/temporalturns.hpp>

namespace Rtx
{
    namespace
    {
        TemporalFlags flags(const bool accumulate, const bool shadow, const bool specular, const bool pane)
        {
            return TemporalFlags{ { accumulate, shadow, specular, pane } };
        }

        struct Frame
        {
            bool mResetBefore;
            TemporalFlags mRuns;
            std::size_t mBefore;
            std::size_t mNow;
            TemporalFlags mFresh;
        };

        /// **Five frames by hand.** The first reads nothing. The second: the glossy filter did not run
        /// on the first, so it is fresh, and the shadow denoiser, which ran, is not — whether it runs
        /// now does not change what it would read. The third: the shadow denoiser skipped the second,
        /// so it is fresh again. The fourth follows a reset and reads nothing, on the half parity
        /// gives it and not the half it began on. The fifth reads everything.
        TEST(RtxTemporalTurnsTest, parityAlternatesAndAFilterIsFreshAfterAFrameItDidNotRunOrAReset)
        {
            const std::array frames{
                Frame{ false, flags(true, true, false, true), 0, 1, flags(true, true, true, true) },
                Frame{ false, flags(true, false, true, true), 1, 0, flags(false, false, true, false) },
                Frame{ false, flags(true, true, true, true), 0, 1, flags(false, true, false, false) },
                Frame{ true, flags(true, true, true, true), 1, 0, flags(true, true, true, true) },
                Frame{ false, flags(true, true, true, true), 0, 1, flags(false, false, false, false) },
            };

            TemporalTurns turns;
            for (std::size_t at = 0; at < frames.size(); ++at)
            {
                const Frame& frame = frames[at];
                if (frame.mResetBefore)
                    turns.reset();

                const TemporalTurns::Step step = turns.next(frame.mRuns);
                EXPECT_EQ(step.mBefore, frame.mBefore) << "frame " << at;
                EXPECT_EQ(step.mNow, frame.mNow) << "frame " << at;
                EXPECT_EQ(step.mRuns.mFlags, frame.mRuns.mFlags) << "frame " << at;
                EXPECT_EQ(step.mFresh.mFlags, frame.mFresh.mFlags) << "frame " << at;
            }
        }
    }
}
