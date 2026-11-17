#include <cstdint>
#include <string>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/longestframes.hpp>
#include <components/rtx/renderer/framespend.hpp>

namespace RtxTool
{
    namespace
    {
        Rtx::FrameSpend spendOf(const double frameMs, const double updateMs)
        {
            Rtx::FrameSpend spend;
            spend.at(Rtx::Timing::Frame) = frameMs;
            spend.at(Rtx::Timing::Update) = updateMs;
            return spend;
        }

        /// **The `sKept` longest frames are kept, longest first, each with the second it ended**: 40 frames of 1 to 40
        /// ms keep 9 to 40, a frame no longer than the least kept is let in nowhere, and one longer takes its place.
        /// The end is a second and its nanoseconds, 12.000000034 for 12,000,000,034 ns; of two alike, the earlier
        /// first.
        TEST(RtxLongestFramesTest, theLongestAreKeptLongestFirstWithTheSecondEachEnded)
        {
            LongestFrames frames;
            EXPECT_EQ(frames.describe(), "");
            for (std::uint32_t at = 1; at <= 40; ++at)
                frames.offer(spendOf(at, 0.5), LongestFrames::Seen{ .mLeg = "leg-1", .mFrame = at, .mEndNs = at });
            frames.offer(spendOf(9.0, 0.0), LongestFrames::Seen{ .mLeg = "leg-2", .mFrame = 1, .mEndNs = 1 });

            const std::string kept = frames.describe();
            EXPECT_EQ(kept.find("leg-2"), std::string::npos) << "a frame no longer than the least kept";
            EXPECT_NE(kept.find("     9.0 ms  leg-1 frame 9,"), std::string::npos);
            EXPECT_EQ(kept.find("     8.0 ms"), std::string::npos);
            EXPECT_LT(kept.find("    40.0 ms  leg-1 frame 40,"), kept.find("    39.0 ms"));

            frames.offer(
                spendOf(250.0, 200.0), LongestFrames::Seen{ .mLeg = "leg-3", .mFrame = 7, .mEndNs = 12'000'000'034 });
            frames.offer(
                spendOf(250.0, 200.0), LongestFrames::Seen{ .mLeg = "leg-3", .mFrame = 8, .mEndNs = 13'000'000'000 });
            const std::string longer = frames.describe();
            EXPECT_EQ(longer.find("     9.0 ms"), std::string::npos) << "the least two let go of";
            EXPECT_EQ(longer.find("    10.0 ms"), std::string::npos);
            EXPECT_EQ(longer.find("\nlongest frames, each ending at a second of the monotonic clock:\n"
                                  "     250.0 ms  leg-3 frame 7, ended 12.000000034: update 200.0"),
                0u);
            EXPECT_LT(longer.find("leg-3 frame 7,"), longer.find("leg-3 frame 8,"));
        }
    }
}
