#include "lightbuilder.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include <components/esm3/loadligh.hpp>
#include <components/fallback/fallback.hpp>
#include <components/rtx/shaders/scene.h>

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

        float channelToLinear(float encoded)
        {
            return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
        }

        /// A weather's colour for one time of day, decoded.
        ///
        /// Absent settings read as black, which is what an unknown weather name should look like —
        /// these come off a file this does not own and cannot be asserted about.
        osg::Vec3f weatherColour(std::string_view weather, std::string_view field, std::string_view phase)
        {
            const osg::Vec4f colour = Fallback::Map::getColour(
                "Weather_" + std::string(weather) + "_" + std::string(field) + "_" + std::string(phase) + "_Color");

            return osg::Vec3f(channelToLinear(colour.x()), channelToLinear(colour.y()), channelToLinear(colour.z()));
        }

        std::string_view nameOf(SkyPhase phase)
        {
            switch (phase)
            {
                case SkyPhase::Night:
                    return "Night";
                case SkyPhase::Sunrise:
                    return "Sunrise";
                case SkyPhase::Day:
                    return "Day";
                case SkyPhase::Sunset:
                    return "Sunset";
            }

            return "Night";
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

    Daylight makeDaylight(std::string_view weather, float hour)
    {
        const float sunrise = Fallback::Map::getFloat("Weather_Sunrise_Time");
        const float nightStart
            = Fallback::Map::getFloat("Weather_Sunset_Time") + Fallback::Map::getFloat("Weather_Sunset_Duration");

        const SkyPhase phase = phaseAt(hour, sunrise, nightStart);
        const std::string_view name = nameOf(phase);

        // One read, two uses: the horizon is fog seen from far enough away, which is why the game
        // records a single colour for both.
        const osg::Vec3f haze = weatherColour(weather, "Fog", name);

        Daylight daylight{
            .mSun = { .mDirection = sunDirection(hour, sunrise, nightStart) },
            .mSkyHorizon = haze,
            .mSkyZenith = weatherColour(weather, "Sky", name),
            .mAmbient = weatherColour(weather, "Ambient", name),
            .mFog = { .mColour = haze,
                .mExtinction = fogExtinction(Fallback::Map::getFloat(
                    "Weather_" + std::string(weather) + "_Land_Fog_" + std::string(name) + "_Depth")) },
        };

        if (phase != SkyPhase::Night)
            daylight.mSun.mIrradiance = weatherColour(weather, "Sun", name) * Rtx::Shaders::DAYLIGHT;

        return daylight;
    }

    osg::Vec3f decodeColour(std::uint32_t packed)
    {
        const auto channel
            = [](std::uint32_t bits) { return channelToLinear(static_cast<float>(bits & 0xFFu) / 255.0f); };

        return osg::Vec3f(channel(packed), channel(packed >> 8), channel(packed >> 16));
    }

    std::optional<Rtx::Light> makeLight(const ESM::Light& record, const osg::Vec3f& position)
    {
        constexpr int notPlaced = ESM::Light::Carry | ESM::Light::Negative | ESM::Light::OffDefault;
        if ((record.mData.mFlags & notPlaced) != 0)
            return std::nullopt;

        // The file is on disk and something else wrote it, so a nonsensical radius is data rather
        // than a broken contract: a light with no size lights nothing and is dropped.
        const auto radius = static_cast<float>(record.mData.mRadius);
        if (!(radius > 0.0f))
            return std::nullopt;

        return Rtx::Light{
            .mPosition = position,
            .mIntensity = decodeColour(record.mData.mColor) * (radius * radius * sIntensity),
            .mReach = radius * sReachScale + sReachBonus,
        };
    }
}
