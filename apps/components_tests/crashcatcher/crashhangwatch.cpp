#include <chrono>
#include <cstdint>

#include <gtest/gtest.h>

#include <components/crashcatcher/crashhangwatch.hpp>

namespace
{
    using namespace std::chrono_literals;

    const std::chrono::steady_clock::time_point sStart{};

    /// A wake at `at` after the watch began.
    Crash::HangAnswer wakeAt(Crash::HangWatch& watch, std::chrono::steady_clock::duration at, std::uint64_t frames,
        std::uint32_t limit, bool held = false)
    {
        return watch.wake(
            Crash::HangWake{ .mNow = sStart + at, .mFrames = frames, .mLimitSeconds = limit, .mHeld = held });
    }

    constexpr Crash::HangQuiet sQuiet;

    TEST(CrashHangWatchTest, aStallIsFoundAtTheLimitAfterTheWakeThatSawTheLastFrame)
    {
        // The frame is seen at 1 s, so the count is 1 s at 2 s and 2 s at 3 s.
        Crash::HangWatch watch(sStart);
        EXPECT_EQ(wakeAt(watch, 1s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 2s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
    }

    TEST(CrashHangWatchTest, aStallIsFoundOnceAndItsEndSaysItsWholeLength)
    {
        // Found at 2 s of stall on the third wake, 3 s of stall on the next, and over on the frame.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 6, 2), Crash::HangAnswer(Crash::HangEnded{ 3 }));
        EXPECT_EQ(wakeAt(watch, 6s, 7, 2), Crash::HangAnswer(sQuiet));

        // A second stall starts its own count: 1 s at 7 s, 2 s at 8 s, at the counter it stopped at.
        EXPECT_EQ(wakeAt(watch, 7s, 7, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 8s, 7, 2), Crash::HangAnswer(Crash::HangFound{ 2, 7 }));
    }

    TEST(CrashHangWatchTest, aStartThatDrawsNothingIsNoHang)
    {
        Crash::HangWatch watch(sStart);
        for (int second = 1; second <= 10; ++second)
            EXPECT_EQ(wakeAt(watch, std::chrono::seconds(second), 0, 2), Crash::HangAnswer(sQuiet));
    }

    TEST(CrashHangWatchTest, aLimitBackOnJustBeforeAWakeDoesNotCountTheWakeBeforeIt)
    {
        // The limit comes back at 2.97 s. The wake at 3 s first sees it and starts the count, which is
        // 1 s at 4 s and 2 s at 5 s: 2.03 s after the limit came back, and never under its 2 s. A watch
        // that added the 3 s wake's step would find the hang at 4 s, 1.03 s after the limit came back.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 2s, 5, 0), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
    }

    TEST(CrashHangWatchTest, aLimitBackOnAfterAFrameSeenWithItOffDoesNotCountTheWakeBeforeIt)
    {
        // The frame is seen at 1 s with the limit off. The wake at 2 s first sees the limit and starts
        // the count, which is 1 s at 3 s and 2 s at 4 s.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 0);
        EXPECT_EQ(wakeAt(watch, 2s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
    }

    TEST(CrashHangWatchTest, aLimitThatStaysOffFindsNothing)
    {
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 0);
        for (int second = 2; second <= 10; ++second)
            EXPECT_EQ(wakeAt(watch, std::chrono::seconds(second), 5, 0), Crash::HangAnswer(sQuiet));
    }

    TEST(CrashHangWatchTest, aFrameAfterTheLimitCameBackIsNoHang)
    {
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 0);
        wakeAt(watch, 3s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 4s, 6, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 6, 2), Crash::HangAnswer(sQuiet));
    }

    TEST(CrashHangWatchTest, aLimitTurnedOffForgetsAStallNotYetFound)
    {
        // 1 s counted at 2 s, forgotten at 3 s, and counted again from the wake at 4 s.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 2);
        wakeAt(watch, 3s, 5, 0);
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 6s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
    }

    TEST(CrashHangWatchTest, aStallAlreadyFoundKeepsItsCountThroughALimitTurnedOff)
    {
        // Found at 2 s of stall, and 3 s of it by the wake with no limit at 4 s, which still counts.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 0), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 6, 0), Crash::HangAnswer(Crash::HangEnded{ 3 }));
    }

    TEST(CrashHangWatchTest, aHeldGamesStallStopsGrowing)
    {
        // 1 s at 2 s, and nothing for the held wakes at 3 s and 4 s. The hold may have ended just
        // before 5 s, so that wake restarts the counting and adds nothing: 2 s only at 6 s.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2, true), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2, true), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 6s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
    }

    TEST(CrashHangWatchTest, aHoldDoesNotGrowAStallAlreadyFound)
    {
        // Found at 3 s with 2 s of stall, held at 4 s, nothing added at 5 s, 1 s added at 6 s: 3 s when
        // the frame comes at 7 s.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 2);
        wakeAt(watch, 2s, 5, 2);
        EXPECT_EQ(wakeAt(watch, 3s, 5, 2), Crash::HangAnswer(Crash::HangFound{ 2, 5 }));
        EXPECT_EQ(wakeAt(watch, 4s, 5, 2, true), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 5s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 6s, 5, 2), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 7s, 6, 2), Crash::HangAnswer(Crash::HangEnded{ 3 }));
    }

    TEST(CrashHangWatchTest, aLateWakeAddsTheLongestWakeAndNoMore)
    {
        // A limit of 3: 2 s of an hour's sleep, under it, then 1 s more to reach it.
        Crash::HangWatch watch(sStart);
        wakeAt(watch, 1s, 5, 3);
        EXPECT_EQ(wakeAt(watch, 3601s, 5, 3), Crash::HangAnswer(sQuiet));
        EXPECT_EQ(wakeAt(watch, 3602s, 5, 3), Crash::HangAnswer(Crash::HangFound{ 3, 5 }));
    }

    TEST(CrashHangWatchTest, onlyAMovedCounterOrAStartNotYetBegunIsNews)
    {
        Crash::HangWatch watch(sStart);
        EXPECT_TRUE(watch.isNews(0));
        wakeAt(watch, 1s, 5, 2);
        EXPECT_FALSE(watch.isNews(5));
        EXPECT_TRUE(watch.isNews(6));
    }
}
