#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/death.hpp>
#include <apps/openmw/mwrender/rtx/traceclocks.hpp>

namespace MWRender
{
    namespace
    {
        /// **Each clock's span since the trace before, apart**: the first trace has none on either, a
        /// paused world's frame moves the host's alone, and a world at half speed moves by half the host's.
        /// Every value a power of two, so each difference is exact in a float.
        TEST(RtxTraceClocksTest, eachClockIsMeasuredFromItsOwnStampAtTheTraceBefore)
        {
            TraceClocks clocks;

            const TracedSeconds first = clocks.trace(10.0, 4.0);
            EXPECT_EQ(first.mHost, 0.0f);
            EXPECT_EQ(first.mWorld, 0.0f);

            // 10.25 - 10.0 on the host, 4.25 - 4.0 in the world.
            const TracedSeconds running = clocks.trace(10.25, 4.25);
            EXPECT_EQ(running.mHost, 0.25f);
            EXPECT_EQ(running.mWorld, 0.25f);

            // The world stood paused: 10.75 - 10.25 on the host, and the world's clock where it was.
            const TracedSeconds paused = clocks.trace(10.75, 4.25);
            EXPECT_EQ(paused.mHost, 0.5f);
            EXPECT_EQ(paused.mWorld, 0.0f);

            // At half the host's rate: 11.75 - 10.75 against 4.75 - 4.25.
            const TracedSeconds slowed = clocks.trace(11.75, 4.75);
            EXPECT_EQ(slowed.mHost, 1.0f);
            EXPECT_EQ(slowed.mWorld, 0.5f);
        }

        /// **A clock that ran back asserts**: neither the loop's clock nor the world's can, so one that did
        /// is a host that broke its contract.
        TEST(RtxTraceClocksTest, aClockThatRanBackAsserts)
        {
            Rtx::Testing::expectAssertDies(
                [] {
                    TraceClocks clocks;
                    clocks.trace(1.0, 1.0);
                    clocks.trace(1.0, 0.5);
                },
                "world >= last->mWorld");
            Rtx::Testing::expectAssertDies(
                [] {
                    TraceClocks clocks;
                    clocks.trace(1.0, 1.0);
                    clocks.trace(0.5, 1.0);
                },
                "host >= last->mHost");
        }
    }
}
