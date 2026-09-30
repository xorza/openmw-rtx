#include <cmath>
#include <numbers>

#include <gtest/gtest.h>

#include <components/sky/skyclock.hpp>

namespace Sky
{
    namespace
    {
        /// At the shipped `timescale` the sky's clock is the simulation's own, bit for bit, which
        /// is what keeps every run made before it existed the same run; at any other it is the
        /// ratio, and a clock that stands or runs backwards holds the sky.
        TEST(SkyClockTest, theShippedTimescaleIsRealTimeAndAnyOtherIsItsRatio)
        {
            constexpr float step = 1.0f / 60.0f;

            EXPECT_EQ(skyStep(step, sVanillaTimeScale), step) << "not near: the same run";
            EXPECT_FLOAT_EQ(skyStep(step, 300.0f), step * 10.0f);
            EXPECT_FLOAT_EQ(skyStep(step, 3000.0f), step * 100.0f);
            EXPECT_FLOAT_EQ(skyStep(step, 15.0f), step * 0.5f);

            EXPECT_EQ(skyStep(step, 0.0f), 0.0f) << "a paused clock holds the sky";
            EXPECT_EQ(skyStep(step, -30.0f), 0.0f) << "and so does one running backwards";
        }

        /// One frame at the shipped scale under Morrowind's fastest deck: the scroll and the sky's
        /// seconds move by the frame. Twice the scale doubles both, so the scale is read; and the
        /// scroll wraps at four, where the sheet repeats.
        TEST(SkyClockTest, theClockStepsTheDeckAndTheSecondsByTheFrame)
        {
            constexpr float step = 1.0f / 60.0f;
            constexpr float cloudSpeed = 400.0f;

            SkyClock shipped;
            shipped.step(step, sVanillaTimeScale, cloudSpeed, false);
            EXPECT_FLOAT_EQ(shipped.mCloudScroll, step);
            EXPECT_DOUBLE_EQ(shipped.mSeconds, static_cast<double>(step));

            SkyClock doubled;
            doubled.step(step, 2.0f * sVanillaTimeScale, cloudSpeed, false);
            EXPECT_FLOAT_EQ(doubled.mCloudScroll, 2.0f * step);
            EXPECT_DOUBLE_EQ(doubled.mSeconds, 2.0 * shipped.mSeconds);

            SkyClock wrapping;
            wrapping.mCloudScroll = 3.99f;
            wrapping.step(step, sVanillaTimeScale, cloudSpeed, false);
            EXPECT_FLOAT_EQ(wrapping.mCloudScroll, 3.99f + step - 4.0f);
        }

        /// **`Weather_Timescale_Clouds` paces the deck by the game's scale over sixty**, as the
        /// rasterizer's `SkyManager` reads it: at the shipped thirty that is half the frame's
        /// seconds, and at sixty the frame's own. The traced deck read the flag nowhere and ran at
        /// twice the rasterizer's pace under content that set it. The sky's seconds are the time-lapse
        /// either way, because the fog drifts on them and the flag is the deck's.
        TEST(SkyClockTest, contentThatTimescalesItsCloudsPacesTheDeckByTheGamesScale)
        {
            constexpr float step = 1.0f / 60.0f;
            constexpr float cloudSpeed = 400.0f;

            EXPECT_FLOAT_EQ(cloudScrollStep(step, cloudSpeed, sVanillaTimeScale, false), step);
            EXPECT_FLOAT_EQ(cloudScrollStep(step, cloudSpeed, sVanillaTimeScale, true), step / 2.0f);
            EXPECT_FLOAT_EQ(cloudScrollStep(step, cloudSpeed, 60.0f, true), step);

            SkyClock timescaled;
            timescaled.step(step, sVanillaTimeScale, cloudSpeed, true);
            EXPECT_FLOAT_EQ(timescaled.mCloudScroll, step / 2.0f);
            EXPECT_DOUBLE_EQ(timescaled.mSeconds, static_cast<double>(step))
                << "the fog's clock is the flag's to leave";

            // At a doubled scale the flag's deck is the frame's seconds, where the time-lapse's
            // would be twice them: the content's word stands over the time-lapse.
            SkyClock doubled;
            doubled.step(step, 2.0f * sVanillaTimeScale, cloudSpeed, true);
            EXPECT_FLOAT_EQ(doubled.mCloudScroll, step);
        }

        /// Once round in four days, counter-clockwise, so negative: a day is a quarter turn, -π/2,
        /// and two days half of one, ±π. Three days are -3π/2, which is the same facing as +π/2,
        /// and four are none at all. A hundred days on is 25 turns, none again, to the float's
        /// own resolution: the angle is taken round in double first.
        TEST(SkyClockTest, theStarsTurnOnceInFourDaysByTheClock)
        {
            constexpr double day = 86400.0;
            constexpr float quarter = std::numbers::pi_v<float> / 2.0f;

            EXPECT_EQ(starRoll(0.0), 0.0f);
            EXPECT_FLOAT_EQ(starRoll(day), -quarter);
            EXPECT_FLOAT_EQ(std::abs(starRoll(2.0 * day)), 2.0f * quarter);
            EXPECT_FLOAT_EQ(starRoll(3.0 * day), quarter);
            EXPECT_NEAR(starRoll(4.0 * day), 0.0f, 1e-6f);
            EXPECT_NEAR(starRoll(100.0 * day + day / 4.0), -quarter / 4.0f, 1e-6f);
        }
    }
}
