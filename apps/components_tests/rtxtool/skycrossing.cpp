#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include <apps/rtxtool/model/skycrossing.hpp>

namespace RtxTool
{
    namespace
    {
        /// As `Rtx::weatherIndex` numbers them.
        constexpr std::uint32_t sClear = 0;
        constexpr std::uint32_t sCloudy = 1;
        constexpr std::uint32_t sRain = 4;
        constexpr std::uint32_t sSnow = 8;

        /// **A press during a crossing turns it at once, and the weather that shows most keeps its
        /// share.** The sky shows `1 - c` of the one it leaves and `c` of the one it goes to.
        ///
        /// By hand: Clear into Rain at 0.3 is 0.7 of Clear, which keeps it when Snow is asked for —
        /// Clear into Snow at 0.3. At 0.7 it is 0.7 of Rain, which keeps it: Rain into Snow at
        /// `1 - 0.7 = 0.3`. At exactly half the crossing starts again from the one it went to. Asked
        /// for the weather it leaves, it turns round where it stands: Clear into Rain at 0.3 is Rain
        /// into Clear at 0.7, the same 0.7 of Clear; and turned round from where nothing had crossed,
        /// it stands where it was.
        TEST(RtxSkyCrossingTest, anAskTurnsTheCrossingAndKeepsTheWeatherThatShowsMost)
        {
            SkyCrossing below(sClear, sRain, 0.3f);
            below.ask(sSnow);
            EXPECT_EQ(below.getWeather(), sClear);
            EXPECT_EQ(below.getNextWeather(), sSnow);
            EXPECT_FLOAT_EQ(below.getCrossed(), 0.3f);

            SkyCrossing past(sClear, sRain, 0.7f);
            past.ask(sSnow);
            EXPECT_EQ(past.getWeather(), sRain);
            EXPECT_EQ(past.getNextWeather(), sSnow);
            EXPECT_FLOAT_EQ(past.getCrossed(), 0.3f);

            SkyCrossing half(sClear, sRain, 0.5f);
            half.ask(sSnow);
            EXPECT_EQ(half.getWeather(), sRain);
            EXPECT_FLOAT_EQ(half.getCrossed(), 0.5f);

            SkyCrossing back(sClear, sRain, 0.3f);
            back.ask(sClear);
            EXPECT_EQ(back.getWeather(), sRain);
            EXPECT_EQ(back.getNextWeather(), sClear);
            EXPECT_FLOAT_EQ(back.getCrossed(), 0.7f);

            SkyCrossing unbegun(sClear, sRain, 0.0f);
            ASSERT_TRUE(unbegun.isCrossing());
            unbegun.ask(sClear);
            EXPECT_FALSE(unbegun.isCrossing()) << "turned round before it began";
            EXPECT_EQ(unbegun.getWeather(), sClear);

            // The weather it goes to asked again changes nothing, and a sky that stands begins a
            // crossing from where it stands.
            SkyCrossing again(sClear, sRain, 0.3f);
            again.ask(sRain);
            EXPECT_EQ(again.getWeather(), sClear);
            EXPECT_FLOAT_EQ(again.getCrossed(), 0.3f);

            SkyCrossing standing(sCloudy, sCloudy, 0.0f);
            standing.ask(sRain);
            EXPECT_EQ(standing.getWeather(), sCloudy);
            EXPECT_EQ(standing.getNextWeather(), sRain);
            EXPECT_EQ(standing.getCrossed(), 0.0f);

            standing.settle(sSnow);
            EXPECT_FALSE(standing.isCrossing());
            EXPECT_EQ(standing.getWeather(), sSnow);
        }

        /// What the world counts is taken as it counts it: a crossing of one weather into itself
        /// stands, one counted whole has landed, and one not begun is a crossing at nought.
        TEST(RtxSkyCrossingTest, theWorldsSkyIsTakenAsItCountsIt)
        {
            const SkyCrossing standing(sClear, sClear, 0.4f);
            EXPECT_FALSE(standing.isCrossing());
            EXPECT_EQ(standing.getCrossed(), 0.0f);

            const SkyCrossing landed(sClear, sRain, 1.0f);
            EXPECT_FALSE(landed.isCrossing());
            EXPECT_EQ(landed.getWeather(), sRain);

            const SkyCrossing begun(sClear, sRain, 0.0f);
            EXPECT_TRUE(begun.isCrossing());
            EXPECT_EQ(begun.getWeather(), sClear);
        }

        /// **The crossing runs at the game clock's speed and lands where it went.** By hand: Clear's
        /// `Transition_Delta` of 0.015 at the game's own `timescale` of 30 crosses 0.015 a second,
        /// a minute and seven seconds for a whole one; at ×8, 240, it crosses 0.12 a second; and a
        /// stopped clock crosses nothing. Four quarters land a crossing on its weather.
        TEST(RtxSkyCrossingTest, aCrossingRunsAtTheClocksSpeedAndLandsWhereItWent)
        {
            EXPECT_FLOAT_EQ(SkyCrossing::shareOf(1.0f, 0.015f, 30.0f), 0.015f);
            EXPECT_FLOAT_EQ(SkyCrossing::shareOf(1.0f, 0.015f, 240.0f), 0.12f);
            EXPECT_FLOAT_EQ(SkyCrossing::shareOf(0.5f, 0.015f, 240.0f), 0.06f);
            EXPECT_EQ(SkyCrossing::shareOf(1.0f, 0.015f, 0.0f), 0.0f);
            EXPECT_NE(SkyCrossing::shareOf(1.0f, 0.015f, 30.0f), SkyCrossing::shareOf(1.0f, 0.03f, 30.0f));

            SkyCrossing crossing(sClear, sRain, 0.0f);
            for (int quarter = 1; quarter <= 3; ++quarter)
            {
                crossing.advance(0.25f);
                EXPECT_FLOAT_EQ(crossing.getCrossed(), 0.25f * quarter);
                EXPECT_EQ(crossing.getWeather(), sClear);
            }
            crossing.advance(0.25f);
            EXPECT_FALSE(crossing.isCrossing());
            EXPECT_EQ(crossing.getWeather(), sRain);
            EXPECT_EQ(crossing.getCrossed(), 0.0f);

            crossing.advance(0.5f);
            EXPECT_FALSE(crossing.isCrossing()) << "a sky that stands moved";
        }

        /// **A key steps from the last weather asked for, so presses in a row walk the list** while
        /// the first is still crossing. Among Clear, Cloudy and Rain, round and round: one on from
        /// Cloudy is Rain and one on from Rain is Clear; one back from Clear is Rain. A weather the
        /// list does not hold, Snow, is before its first going on and after its last going back.
        TEST(RtxSkyCrossingTest, aKeyStepsFromTheLastWeatherAskedFor)
        {
            const std::array<std::uint32_t, 3> rolled{ sClear, sCloudy, sRain };

            const SkyCrossing cloudy(sCloudy, sCloudy, 0.0f);
            EXPECT_EQ(cloudy.stepAmong(rolled, 1), 2u);
            EXPECT_EQ(cloudy.stepAmong(rolled, 2), 0u);
            EXPECT_EQ(cloudy.stepAmong(rolled, -1), 0u);
            EXPECT_EQ(SkyCrossing(sRain, sRain, 0.0f).stepAmong(rolled, 1), 0u);
            EXPECT_EQ(SkyCrossing(sClear, sClear, 0.0f).stepAmong(rolled, -1), 2u);

            const SkyCrossing snow(sSnow, sSnow, 0.0f);
            EXPECT_EQ(snow.stepAmong(rolled, 1), 0u);
            EXPECT_EQ(snow.stepAmong(rolled, -1), 2u);

            // Two presses a tenth of a crossing apart: the second lands on Rain at once, from the
            // Clear the sky still mostly is, where the game's queue kept Cloudy crossing in.
            SkyCrossing sky(sClear, sClear, 0.0f);
            sky.ask(rolled[sky.stepAmong(rolled, 1)]);
            sky.advance(0.1f);
            sky.ask(rolled[sky.stepAmong(rolled, 1)]);
            EXPECT_EQ(sky.getWeather(), sClear);
            EXPECT_EQ(sky.getNextWeather(), sRain);
            EXPECT_FLOAT_EQ(sky.getCrossed(), 0.1f);
        }
    }
}
