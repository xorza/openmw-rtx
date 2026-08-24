#include "lighting.hpp"

#include <components/rtx/shaders/visibility.h>
#include <components/rtxbridge/frameworld.hpp>
#include <components/rtxbridge/moonbuilder.hpp>

namespace RtxTool
{
    void relight(CellLighting& lighting, std::string_view weather, int day, float hour)
    {
        if (!lighting.mOutdoors)
            return;

        const RtxBridge::Daylight daylight = RtxBridge::makeDaylight(weather, hour);

        lighting.mAmbient = daylight.mAmbient;
        lighting.mDaylight = daylight;
        lighting.mFog = daylight.mFog;
        lighting.mDay = day;
        lighting.mHour = hour;

        // The name reached `makeDaylight` intact, so it is one of the ten.
        lighting.mWeather = RtxBridge::weatherIndex(weather).value();
        lighting.mWindSpeed = RtxBridge::windSpeed(weather);
    }

    void applyLighting(const CellLighting& lighting, Rtx::Shaders::VisibilityConstants& constants)
    {
        RtxBridge::FrameWorld world{
            .mSunDirection = lighting.mDaylight.mSun.mDirection,
            .mSunIrradiance = lighting.mDaylight.mSun.mIrradiance,
            .mAmbient = lighting.mAmbient,
            .mSkyHorizon = lighting.mDaylight.mSkyHorizon,
            .mSkyZenith = lighting.mDaylight.mSkyZenith,
            .mAir = lighting.mFog,
            .mWaterLevel = lighting.mWaterLevel,
            .mSeconds = lighting.mSeconds,

            // **The same weather twice, at a blend of nothing.** A harness frame is a settled sky:
            // there is no weather system here to be halfway between two of them, so there is no
            // second one to name.
            .mWeather = lighting.mWeather,
            .mNextWeather = lighting.mWeather,
            .mWeatherBlend = 0.0f,

            .mWindSpeed = lighting.mWindSpeed,

            // **Asked of the eye, which is the only body standing in this weather.** The game aims
            // an ashstorm at the player; every caller here has already put its camera in `mOrigin`,
            // so the same rule reaches the same answer for whoever is looking.
            .mStormDirection = RtxBridge::stormDirection(lighting.mWeather, constants.mOrigin),
        };

        // **Left where a default leaves them for a room**, which is an alpha of nothing and so a
        // disc the sky skips: an interior has no moons over it, and `relight` will not put any there.
        if (lighting.mOutdoors)
            for (const RtxBridge::Moon moon : { RtxBridge::Moon::Masser, RtxBridge::Moon::Secunda })
                world.mMoons[static_cast<std::size_t>(moon)] = RtxBridge::makeMoon(moon, lighting.mDay, lighting.mHour);

        RtxBridge::applyWorld(world, constants);
    }
}
