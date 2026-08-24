#include "lighting.hpp"

#include <components/rtx/shaders/visibility.h>
#include <components/rtxbridge/frameworld.hpp>
#include <components/rtxbridge/moonbuilder.hpp>

namespace RtxTool
{
    namespace
    {
        void settle(CellLighting& lighting, const RtxBridge::Daylight& daylight, int day, float hour)
        {
            lighting.mAmbient = daylight.mAmbient;
            lighting.mDaylight = daylight;
            lighting.mFog = daylight.mFog;
            lighting.mDay = day;
            lighting.mHour = hour;
        }
    }

    void relight(CellLighting& lighting, std::string_view weather, int day, float hour)
    {
        if (!lighting.mOutdoors)
            return;

        settle(lighting, RtxBridge::makeDaylight(weather, hour), day, hour);

        // The name reached `makeDaylight` intact, so it is one of the ten.
        lighting.mWeather = RtxBridge::weatherIndex(weather).value();
        lighting.mNextWeather = lighting.mWeather;
        lighting.mWeatherBlend = 0.0f;
        lighting.mWindSpeed = RtxBridge::windSpeed(weather);
    }

    void relight(CellLighting& lighting, std::string_view from, std::string_view to, float blend, int day, float hour)
    {
        if (!lighting.mOutdoors)
            return;

        settle(lighting, RtxBridge::makeDaylight(from, to, blend, hour), day, hour);

        lighting.mWeather = RtxBridge::weatherIndex(from).value();
        lighting.mNextWeather = RtxBridge::weatherIndex(to).value();
        lighting.mWeatherBlend = blend;

        // The wind is one of the quantities the engine mixes across a transition too.
        lighting.mWindSpeed = RtxBridge::windSpeed(from) * (1.0f - blend) + RtxBridge::windSpeed(to) * blend;
    }

    void applyLighting(const CellLighting& lighting, Rtx::Shaders::VisibilityConstants& constants)
    {
        RtxBridge::FrameWorld world{
            .mSun = lighting.mDaylight.mSun,
            .mAmbient = lighting.mAmbient,
            .mSkyHorizon = lighting.mDaylight.mSkyHorizon,
            .mSkyZenith = lighting.mDaylight.mSkyZenith,
            .mAir = lighting.mFog,
            .mWaterLevel = lighting.mWaterLevel,
            .mSeconds = lighting.mSeconds,

            // A settled sky names the same weather twice at a blend of nothing, which is what a
            // `shot` always is; a window running a transition says where it has got to.
            .mWeather = lighting.mWeather,
            .mNextWeather = lighting.mNextWeather,
            .mWeatherBlend = lighting.mWeatherBlend,

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
            {
                RtxBridge::MoonPlacement placed = RtxBridge::makeMoon(moon, lighting.mDay, lighting.mHour);
                placed.mFace = lighting.mFaces.of(moon);
                world.mMoons[static_cast<std::size_t>(moon)] = placed;
            }

        RtxBridge::applyWorld(world, constants);
    }
}
