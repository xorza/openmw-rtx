#include "lightbuilder.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

#include <components/esm3/loadligh.hpp>
#include <components/esm3/loadregn.hpp>
#include <components/fallback/fallback.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/visibility.h>
#include <components/sky/timeofday.hpp>

namespace RtxBridge
{
    namespace
    {
        /// How bright a light is at half its recorded radius.
        ///
        /// There is no value in the record to be faithful to, so this is the whole of the scale and
        /// it was set by eye. Provisional in a specific way: vanilla textures have light painted
        /// into them already, so every lamp here is competing with illumination that is in the
        /// albedo, and this number only starts to mean something once that is unpicked.
        ///
        /// The pi is the Lambertian `1/pi` the shader divides by, and cancels against it exactly —
        /// so a lamp is measured on the scale below and the sun, which carries no such factor, is
        /// measured a pi apart from it. Both sides read the one constant.
        const float sIntensity = 0.25f * Rtx::Shaders::PI;

        /// How much further a light reaches than its record says, and how much further again.
        ///
        /// Morrowind's radii run 64 to 256 units in an interior — a metre to three and a half at
        /// seventy units to the metre — so a lantern lights its own post and nothing else. That was
        /// a fixed falloff curve in a renderer with no bounce, where an ambient term filled the
        /// room; here the ambient is real light and the lamps have to be what lights the place.
        ///
        /// Scaling alone widens the gap it is meant to close: a candle's 64 units doubles to 128,
        /// which is still nothing, while a lantern's 256 gains a whole lantern's worth. The flat
        /// term narrows the two instead, and it is the candles that most need to leave their table.
        constexpr float sReachScale = 2.0f;
        constexpr float sReachBonus = 128.0f;

        /// Morrowind's own sun, out of `apps/openmw/mwworld/weather.cpp:901`'s
        /// `(-400 * orbit, 75, -100)` — how far it swings east to west, how far north it sits, and
        /// how far down it looks at noon. The vector is where the light *goes*, so the negative z is
        /// the sun being above the world rather than below it.
        constexpr float sSwing = 400.0f;
        constexpr float sNorthing = 75.0f;
        constexpr float sClimb = 100.0f;

        /// Morrowind's ten weathers, in `MWWorld::WeatherManager`'s registration order — which is
        /// what a script id counts along and what a `Weather_<name>_*` key spells. The shader names
        /// the same order as `WEATHER_*`; this is the only place the spellings live.
        constexpr std::array<std::string_view, Rtx::Shaders::WEATHER_COUNT> sWeathers = {
            "Clear",
            "Cloudy",
            "Foggy",
            "Overcast",
            "Rain",
            "Thunderstorm",
            "Ashstorm",
            "Blight",
            "Snow",
            "Blizzard",
        };

        /// Where the ash comes from, out of `apps/openmw/mwworld/weather.cpp:55`. Flat, because the
        /// direction it drives is taken on the ground plane and never points up the mountain.
        constexpr float sRedMountainX = 25000.0f;
        constexpr float sRedMountainY = 70000.0f;

        float channelToLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

    }

    SkyPhase phaseAt(float hour, float sunrise, float nightStart)
    {
        // How long either end of the day counts as its own phase. The game ramps across a window
        // this wide from its own settings; this steps in the middle of one, which is the single
        // stand-in here and the reason an hour inside a transition is the only hour that differs.
        constexpr float sTransition = 1.0f;

        if (hour < sunrise - sTransition || hour > nightStart + sTransition)
            return SkyPhase::Night;
        if (hour <= sunrise + sTransition)
            return SkyPhase::Sunrise;
        if (hour >= nightStart - sTransition)
            return SkyPhase::Sunset;

        return SkyPhase::Day;
    }

    osg::Vec3f sunDirection(float hour, float sunrise, float nightStart)
    {
        const bool night = phaseAt(hour, sunrise, nightStart) == SkyPhase::Night;
        const float duration = night ? 24.0f - (nightStart - sunrise) : nightStart - sunrise;
        const float since = night ? std::fmod(hour - nightStart + 24.0f, 24.0f) : hour - sunrise;
        const float travelled = duration > 0.0f ? since / duration : 0.0f;
        const float orbit = night ? 2.0f * travelled - 1.0f : 1.0f - 2.0f * travelled;

        osg::Vec3f direction(-sSwing * orbit, sNorthing, -sClimb);
        direction.normalize();
        return direction;
    }

    namespace
    {
        /// One weather's numbers at one hour, before any of them is converted.
        ///
        /// **Kept in the units the file records them in**, because a transition lerps *these* and not
        /// what they become: the game blends the fog's recorded depth and converts once
        /// (`apps/openmw/mwworld/weather.cpp:1090`), and blending two extinctions instead is a
        /// different curve.
        struct Reading
        {
            osg::Vec3f mHaze;
            osg::Vec3f mSky;
            osg::Vec3f mAmbient;
            osg::Vec3f mSun;
            float mFogDepth = 0.0f;
        };

        Reading readWeather(std::string_view weather, const Sky::TimeOfDaySettings& times, float hour)
        {
            const std::string name(weather);

            // **The game's own four-point ramp rather than a step between four phases.** Each
            // quantity crosses dawn over a window of its own — the sun can be up before the sky has
            // finished turning — so reading whichever phase an hour fell in got every hour inside a
            // transition wrong, which is most of sunrise and most of dusk.
            const auto ramp = [&name, &times, hour](std::string_view field) {
                const std::string prefix(field);
                const auto colour = [&name, &prefix](std::string_view phase) {
                    return Fallback::Map::getColour(
                        "Weather_" + name + "_" + prefix + "_" + std::string(phase) + "_Color");
                };

                return decodeColour(Sky::TimeOfDayInterpolator<osg::Vec4f>(
                    colour("Sunrise"), colour("Day"), colour("Sunset"), colour("Night"))
                        .getValue(hour, times, prefix));
            };

            // **A content file records a day depth and a night one and nothing between**, so the
            // game hands the day value to three of the ramp's four points and lets it cross to night
            // at dusk. Asking for a sunrise depth is not a key that reads zero — the fallback map
            // does not know it at all and throws.
            const float day = Fallback::Map::getFloat("Weather_" + name + "_Land_Fog_Day_Depth");
            const float night = Fallback::Map::getFloat("Weather_" + name + "_Land_Fog_Night_Depth");

            return Reading{
                .mHaze = ramp("Fog"),
                .mSky = ramp("Sky"),
                .mAmbient = ramp("Ambient"),
                .mSun = ramp("Sun"),
                .mFogDepth = Sky::TimeOfDayInterpolator<float>(day, day, day, night).getValue(hour, times, "Fog"),
            };
        }

        Daylight settle(const Reading& read, const Sky::TimeOfDaySettings& times, float hour)
        {
            return Daylight{
                .mSun = { .mDirection = sunDirection(hour, times.mNightEnd, times.mNightStart),

                    // **Not switched off at night, because the game does not switch it off either.**
                    // `WeatherManager::calculateResult` takes the sun's colour straight off this
                    // ramp, and its night value is a dim blue rather than nothing.
                    .mIrradiance = read.mSun * Rtx::Shaders::DAYLIGHT },

                // The disc is the other question and the engine answers it by the hour.
                .mSunVisible = hour > times.mNightEnd && hour < times.mNightStart,
                .mSkyHorizon = read.mHaze,
                .mSkyZenith = read.mSky,
                .mAmbient = read.mAmbient,
                .mFog = { .mColour = read.mHaze, .mExtinction = fogExtinction(read.mFogDepth) },
            };
        }
    }

    Daylight makeDaylight(std::string_view weather, float hour)
    {
        const Sky::TimeOfDaySettings times = Sky::TimeOfDaySettings::fromFallback();
        return settle(readWeather(weather, times, hour), times, hour);
    }

    Daylight makeDaylight(std::string_view from, std::string_view to, float blend, float hour)
    {
        const Sky::TimeOfDaySettings times = Sky::TimeOfDaySettings::fromFallback();
        const Reading a = readWeather(from, times, hour);
        const Reading b = readWeather(to, times, hour);

        const auto mix = [blend](const auto& x, const auto& y) { return x * (1.0f - blend) + y * blend; };

        // Exactly the quantities `calculateTransitionResult` blends, and the depth among them rather
        // than the extinction it becomes.
        return settle(
            Reading{
                .mHaze = mix(a.mHaze, b.mHaze),
                .mSky = mix(a.mSky, b.mSky),
                .mAmbient = mix(a.mAmbient, b.mAmbient),
                .mSun = mix(a.mSun, b.mSun),
                .mFogDepth = mix(a.mFogDepth, b.mFogDepth),
            },
            times, hour);
    }

    std::optional<std::uint32_t> weatherIndex(std::string_view weather)
    {
        const auto found = std::find(sWeathers.begin(), sWeathers.end(), weather);
        if (found == sWeathers.end())
            return std::nullopt;

        return static_cast<std::uint32_t>(found - sWeathers.begin());
    }

    std::uint32_t nextRegionWeather(const ESM::Region* region, std::uint32_t weather, bool forward)
    {
        const std::uint32_t count = static_cast<std::uint32_t>(sWeathers.size());
        const std::uint32_t step = forward ? 1u : count - 1u;

        // Round once and no further: a region with nothing to offer hands back a step of the plain
        // order rather than spinning, which is what a record of all zeroes would otherwise do.
        std::uint32_t at = (weather + step) % count;
        for (std::uint32_t tried = 0; tried < count; ++tried)
        {
            if (region == nullptr || region->mData.mProbabilities[at] > 0)
                return at;

            at = (at + step) % count;
        }

        return (weather + step) % count;
    }

    std::string_view weatherName(std::uint32_t weather)
    {
        return weather < sWeathers.size() ? sWeathers[weather] : std::string_view();
    }

    float windSpeed(std::string_view weather)
    {
        return Fallback::Map::getFloat("Weather_" + std::string(weather) + "_Wind_Speed");
    }

    osg::Vec3f stormDirection(std::uint32_t weather, const osg::Vec3f& observer)
    {
        // `MWWorld::Weather::defaultDirection`, which is due north and what every weather that
        // carries nothing still blows along.
        const osg::Vec3f north(0.0f, 1.0f, 0.0f);
        if (weather != Rtx::Shaders::WEATHER_ASHSTORM && weather != Rtx::Shaders::WEATHER_BLIGHT)
            return north;

        osg::Vec3f away(observer.x() - sRedMountainX, observer.y() - sRedMountainY, 0.0f);

        // Standing on the summit, where the direction away from it is no direction at all.
        if (away.normalize() == 0.0f)
            return north;

        return away;
    }

    osg::Vec3f decodeColour(const osg::Vec4f& encoded)
    {
        return osg::Vec3f(channelToLinear(encoded.x()), channelToLinear(encoded.y()), channelToLinear(encoded.z()));
    }

    osg::Vec3f decodeColour(std::uint32_t packed)
    {
        const auto channel
            = [](std::uint32_t bits) { return channelToLinear(static_cast<float>(bits & 0xFFu) / 255.0f); };

        return osg::Vec3f(channel(packed), channel(packed >> 8), channel(packed >> 16));
    }

    std::optional<Rtx::Light> makeLight(const osg::Vec3f& colour, float radius, const osg::Vec3f& position)
    {
        // The radius comes off a file something else wrote, or off a graph something else built, so
        // a nonsensical one is data rather than a broken contract: a light with no size lights
        // nothing and is dropped.
        if (!(radius > 0.0f))
            return std::nullopt;

        return Rtx::Light{
            .mPosition = position,
            .mIntensity = colour * (radius * radius * sIntensity),
            .mReach = radius * sReachScale + sReachBonus,
        };
    }

    std::optional<Rtx::Light> makeLight(const ESM::Light& record, const osg::Vec3f& position)
    {
        constexpr int notPlaced = ESM::Light::Carry | ESM::Light::Negative | ESM::Light::OffDefault;
        if ((record.mData.mFlags & notPlaced) != 0)
            return std::nullopt;

        return makeLight(decodeColour(record.mData.mColor), static_cast<float>(record.mData.mRadius), position);
    }
}
