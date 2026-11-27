#include <cstdint>

#include <gtest/gtest.h>

#include <components/rtx/frame/historylength.hpp>

namespace Rtx
{
    namespace
    {
        /// **A history is a span of time over a smoothed frame time**, NRD's low pass. With no clock a frame is a
        /// sixtieth, so a quarter of a second is 15 frames and half of one 30. One frame of a thirtieth moves the
        /// smoothed time by a thirteenth of the step, the previous rate's sixty times 0.2 spreading it over twelve
        /// frames more: `1/60 + (1/30 - 1/60) / 13` = 0.017949, so a quarter of a second is 13.93 frames, rounded
        /// to 14. Held at a thirtieth it settles to 7.5, rounded up to 8; a frame no clock measured moves nothing,
        /// and a span shorter than a frame keeps one.
        TEST(RtxHistoryLengthTest, aHistoryIsASpanOfTimeOverASmoothedFrameTime)
        {
            HistoryLength length;
            EXPECT_EQ(length.advance(0.0f, 0.25f), 15.0f);
            EXPECT_EQ(length.advance(0.0f, 0.5f), 30.0f) << "the span matters";

            EXPECT_EQ(length.advance(1.0f / 30.0f, 0.25f), 14.0f) << "one slow frame moves it by a thirteenth";
            EXPECT_EQ(length.advance(0.0f, 0.25f), 14.0f) << "a frame no clock measured moves nothing";

            float settled = 0.0f;
            for (std::uint32_t frame = 0; frame < 200; ++frame)
                settled = length.advance(1.0f / 30.0f, 0.25f);
            EXPECT_EQ(settled, 8.0f) << "a quarter of a second at thirty frames";

            EXPECT_EQ(length.advance(0.0f, 0.001f), 1.0f) << "a span shorter than a frame keeps one";
        }
    }
}
