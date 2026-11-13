#include <variant>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashmonitorrequests.hpp>

namespace
{
    /// **A question is taken in order, but never ahead of the game's end**, and every question the end passed, or
    /// that came after it, is counted once: a hang's dump that outlasted the game posted its question after the
    /// handler said the game was gone, and the monitor dropped it without a word in the log.
    TEST(CrashMonitorRequestsTest, theGamesEndGoesFirstAndEveryQuestionItPassedIsCounted)
    {
        Crash::MonitorRequests requests;
        requests.post(Crash::AskToEnd{ .mSeconds = 2, .mStalledAt = 5, .mDumped = true });
        const Crash::MonitorRequest asked = requests.take();
        ASSERT_TRUE(std::holds_alternative<Crash::AskToEnd>(asked)) << "a question alone is taken";
        EXPECT_EQ(std::get<Crash::AskToEnd>(asked).mStalledAt, 5u);
        EXPECT_EQ(requests.takeUnasked(), 0u) << "the question taken is no longer waiting";

        // The question first and the end behind it: the end is taken, and the question waits.
        requests.post(Crash::AskToEnd{ .mSeconds = 2, .mStalledAt = 7, .mDumped = true });
        requests.post(Crash::HandlerReturned{ .mResult = 3 });
        const Crash::MonitorRequest ended = requests.take();
        ASSERT_TRUE(std::holds_alternative<Crash::HandlerReturned>(ended));
        EXPECT_EQ(std::get<Crash::HandlerReturned>(ended).mResult, 3);

        // A question the watch posted after the end, as a slow dump's is.
        requests.post(Crash::AskToEnd{ .mSeconds = 3, .mStalledAt = 9, .mDumped = false });
        EXPECT_EQ(requests.takeUnasked(), 2u) << "the one the end passed and the one after it";
        EXPECT_EQ(requests.takeUnasked(), 0u) << "each counted once";

        // An overdue handler goes first as well.
        requests.post(Crash::AskToEnd{ .mSeconds = 2, .mStalledAt = 1, .mDumped = true });
        requests.post(Crash::HandlerOverdue{});
        EXPECT_TRUE(std::holds_alternative<Crash::HandlerOverdue>(requests.take()));
        EXPECT_EQ(requests.takeUnasked(), 1u);
    }
}
