#include <gtest/gtest.h>

#include <components/fallback/fallback.hpp>
#include <components/settings/values.hpp>
#include <components/weather/downpour.hpp>

namespace Weather
{
    namespace
    {
        /// The two constants the game reads from its own store rather than the ini.
        constexpr float sStormWind = 50.0f;
        constexpr float sGravity = 500.0f;

        /// Seeds the handful of keys these tests read.
        ///
        /// **`Fallback::Map` keeps the first value a key is given**, and a test elsewhere in this
        /// binary opens the real installation — so which of the two any read below gets depends on
        /// the order the suite ran in, and what is asserted has to be true of either. These seeds
        /// are the shipped values for exactly that reason.
        void seed()
        {
            Fallback::Map::init({
                { "Weather_Clear_Using_Precip", "0" },
                { "Weather_Rain_Using_Precip", "1" },
                { "Weather_Thunderstorm_Using_Precip", "1" },
                { "Weather_Clear_Wind_Speed", "0.3" },
                { "Weather_Ashstorm_Wind_Speed", "0.8" },
            });
        }

        /// Which weathers drop rain is a fact about the content files, not about this code.
        ///
        /// **`Weather_<name>_Using_Precip` is the whole of the question** — Morrowind hard-codes the
        /// answer per weather and the engine draws a particle system rather than the mesh the
        /// setting names. Rain and thunder carry it, fair weather does not, and a harness that reads
        /// them the other way round would render a clear sky full of drops.
        TEST(WeatherDownpourTest, onlyTheWetWeathersCarryRain)
        {
            seed();

            EXPECT_TRUE(downpourAt("Clear", sStormWind, sGravity).mRainEffect.empty());
            EXPECT_FALSE(downpourAt("Rain", sStormWind, sGravity).mRainEffect.empty());
            EXPECT_FALSE(downpourAt("Thunderstorm", sStormWind, sGravity).mRainEffect.empty());
        }

        /// A storm is a wind speed over a threshold, and the threshold is the caller's.
        ///
        /// Proving the parameter matters rather than that the call returns: the same weather is a
        /// storm under one threshold and not under another, so the number reached the comparison.
        TEST(WeatherDownpourTest, aStormIsWhateverBlowsHarderThanTheThreshold)
        {
            seed();

            // Read rather than pinned, so the two thresholds below straddle whichever value won.
            const float ash = windSpeed("Ashstorm");
            ASSERT_GT(ash, 0.0f);

            EXPECT_TRUE(downpourAt("Ashstorm", ash * 0.5f, sGravity).mIsStorm);
            EXPECT_FALSE(downpourAt("Ashstorm", ash * 2.0f, sGravity).mIsStorm);
        }

        /// Every weather's rain falls at the gravity it was handed, and nothing else invents one.
        TEST(WeatherDownpourTest, rainFallsAtTheGravityItWasGiven)
        {
            EXPECT_FLOAT_EQ(downpourAt("Rain", sStormWind, sGravity).mRainSpeed, sGravity);
            EXPECT_FLOAT_EQ(downpourAt("Rain", sStormWind, 12.0f).mRainSpeed, 12.0f);
        }

        /// Four weathers drive a model past the eye and the other six drive nothing.
        ///
        /// **The table the game's weather manager used to keep on its own.** Two copies of four
        /// entries is two chances to disagree about what a blizzard looks like.
        TEST(WeatherDownpourTest, fourWeathersDriveAModelAndTheRestDriveNone)
        {
            EXPECT_EQ(stormEffect("Ashstorm"), Settings::models().mWeatherashcloud.get());
            EXPECT_EQ(stormEffect("Blight"), Settings::models().mWeatherblightcloud.get());
            EXPECT_EQ(stormEffect("Snow"), Settings::models().mWeathersnow.get());
            EXPECT_EQ(stormEffect("Blizzard"), Settings::models().mWeatherblizzard.get());

            EXPECT_TRUE(stormEffect("Clear").empty());
            EXPECT_TRUE(stormEffect("Rain").empty());

            // Read out of a weather rather than beside it, because that is how `Precipitation` gets
            // it and a table consulted only by its own test proves nothing.
            EXPECT_EQ(downpourAt("Blizzard", sStormWind, sGravity).mParticleEffect,
                Settings::models().mWeatherblizzard.get());
        }
    }
}
