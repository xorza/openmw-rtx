#include <gtest/gtest.h>

#include <components/rtxvulkan/frametracer.hpp>

namespace Rtx
{
    namespace
    {
        /// **A chain made again loses the past as an extent that moved does**: its histories and its air's volume
        /// start empty, at whatever extents. Under an upscaler that filters the light, Native against Off makes the
        /// chain again and moves no extent, and the frame after once blended the empty volume as its past. The
        /// upscaler's targets and the arena alone lose nothing the frame reprojects.
        TEST(RtxFrameTracerTest, aChainMadeAgainLosesThePastAsAnExtentThatMovedDoes)
        {
            using Moves = FrameTracer::Moves;
            EXPECT_FALSE(Moves{}.losesPast());
            EXPECT_TRUE(Moves{ .mRenderExtent = true }.losesPast());
            EXPECT_TRUE(Moves{ .mOutputExtent = true }.losesPast());
            EXPECT_TRUE(Moves{ .mTrace = true }.losesPast());
            EXPECT_FALSE((Moves{ .mUpscaler = true, .mArena = true }).losesPast());
        }
    }
}
