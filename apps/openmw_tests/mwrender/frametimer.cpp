#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include <apps/components_tests/rtx/support/allocations.hpp>
#include <apps/openmw/mwrender/framesaside.hpp>
#include <apps/openmw/mwrender/rtx/frametimer.hpp>

namespace MWRender
{
    namespace
    {
        /// A second closes on the frame that fills it, the title describes exactly that second, and
        /// the next second starts from nothing — without reaching the heap on the frame that closes
        /// one, which is a frame like any other.
        TEST(RtxFrameTimerTest, aSecondOfFramesClosesOneRateAndTheNextStartsFromNothing)
        {
            FrameTimer timer;
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX") << "no second has run out";

            // Ninety-nine frames of ten milliseconds are 990 ms, which is short of a second.
            for (std::int32_t at = 0; at < 99; ++at)
                EXPECT_FALSE(timer.addFrame(10.0)) << "frame " << at + 1 << " closed a second early";
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX") << "an open second has no rate";

            bool closed = false;
            std::string_view title;
            const std::size_t spent = Rtx::Testing::allocationsIn([&] {
                closed = timer.addFrame(10.0);
                title = timer.writeTitle({});
            });

            EXPECT_TRUE(closed) << "the hundredth frame is the second";
            EXPECT_EQ(spent, 0u) << "closing a second reached the heap " << spent << " times";
            EXPECT_EQ(title, "OpenMW RTX - 100 fps, 10.0 ms, worst 10.0 ms");

            // Forty frames alternating 5 and 45 ms sum to 20 x 5 + 20 x 45 = 1000 exactly, with a
            // mean of 25 — so the worst is the figure the mean hides. The first thirty-nine are
            // 20 x 5 + 19 x 45 = 955, which is still open.
            for (std::int32_t at = 0; at < 39; ++at)
                EXPECT_FALSE(timer.addFrame(at % 2 == 0 ? 5.0 : 45.0)) << "frame " << at + 1 << " of the second second";
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX - 100 fps, 10.0 ms, worst 10.0 ms")
                << "an open second leaves the rate alone";

            EXPECT_TRUE(timer.addFrame(45.0));
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX - 40 fps, 25.0 ms, worst 45.0 ms")
                << "the first second's worst did not carry over";

            // A rate that does not divide a second: 117 x 8.5 = 994.5 is open and 118 x 8.5 = 1003
            // closes, at 1000 / 8.5 = 117.6 frames a second rounded to the nearest whole one.
            for (std::int32_t at = 0; at < 117; ++at)
                EXPECT_FALSE(timer.addFrame(8.5));
            EXPECT_TRUE(timer.addFrame(8.5));
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX - 118 fps, 8.5 ms, worst 8.5 ms");

            // One frame longer than a second is a second on its own: 1000 / 1500 rounds to one.
            EXPECT_TRUE(timer.addFrame(1500.0));
            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX - 1 fps, 1500.0 ms, worst 1500.0 ms");
        }

        /// Ten frames of a hundred milliseconds close a second, and the title of that second is the
        /// rate, then the run's note where it has one — left out where there is nothing to say, so a
        /// played game's title is the rate alone.
        TEST(RtxFrameTimerTest, theTitleIsTheRateThenTheNote)
        {
            FrameTimer timer;
            for (std::int32_t frame = 0; frame < 9; ++frame)
                EXPECT_FALSE(timer.addFrame(100.0)) << "frame " << frame << " of a second not yet run out";
            ASSERT_TRUE(timer.addFrame(100.0));

            EXPECT_EQ(std::string_view(timer.writeTitle({})), "OpenMW RTX - 10 fps, 100.0 ms, worst 100.0 ms");

            const std::string_view whole = timer.writeTitle("Thunderstorm, 14:32");
            EXPECT_EQ(whole, "OpenMW RTX - 10 fps, 100.0 ms, worst 100.0 ms - Thunderstorm, 14:32");
            EXPECT_EQ(whole.data()[whole.size()], '\0') << "ends in the nought a C string wants";
        }

        /// A note past the room is cut short at `sTitleLengthMost` characters, and the title still reaches no heap.
        TEST(RtxFrameTimerTest, aLongNoteIsCutShortAtTheMostATitleHolds)
        {
            FrameTimer timer;
            const std::string note(200, 'x');
            std::string_view title;
            const std::size_t spent = Rtx::Testing::allocationsIn([&] { title = timer.writeTitle(note); });

            EXPECT_EQ(spent, 0u);
            // "OpenMW RTX - " is 13 characters, so 127 - 13 = 114 of the note's 200 fit.
            EXPECT_EQ(title, "OpenMW RTX - " + std::string(114, 'x'));
            EXPECT_EQ(title.size(), FrameTimer::sTitleLengthMost);
            EXPECT_EQ(title.data()[title.size()], '\0');
        }

        /// **The gap between two frames of the world is the update, the interface's own frames and what stood
        /// hidden, summing to it exactly**: the interface's presents are `Present`'s and come out of its span, and
        /// the next gap starts with none. Left at 100 ms and arriving at 180: 15 ms hidden and 40 of the
        /// interface's, 6 of them presents, leave 180 − 100 − 40 − 15 = 25 of update and 40 − 6 = 34 of the
        /// interface, 6 presented beside the world's own 2.
        TEST(RtxFrameTimerTest, aGapIsTheUpdateTheInterfaceAndTheHiddenSummingToIt)
        {
            using std::chrono::milliseconds;
            const std::chrono::steady_clock::time_point origin;
            FrameTimer timer;
            timer.addPresent(2.0);
            timer.leave(origin + milliseconds(100));
            timer.addInterfacePresent(milliseconds(4));
            timer.addInterfacePresent(milliseconds(2));

            const FrameTimer::Gap gap = timer.gapTo(
                origin + milliseconds(180), AsideSpan{ .mInterface = milliseconds(40), .mHidden = milliseconds(15) });
            EXPECT_EQ(gap.mUpdateMs, 25.0);
            EXPECT_EQ(gap.mInterfaceMs, 34.0);
            EXPECT_EQ(gap.mHiddenMs, 15.0);
            EXPECT_EQ(timer.takePresent(), 8.0) << "the world's present and the interface's";

            timer.leave(origin + milliseconds(200));
            const FrameTimer::Gap plain = timer.gapTo(origin + milliseconds(210), AsideSpan{});
            EXPECT_EQ(plain.mUpdateMs, 10.0);
            EXPECT_EQ(plain.mInterfaceMs, 0.0) << "the last gap's interface presents were carried";
            EXPECT_EQ(plain.mHiddenMs, 0.0);
        }
    }
}
