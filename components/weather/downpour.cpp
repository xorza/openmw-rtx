#include "downpour.hpp"

#include <components/fallback/fallback.hpp>
#include <components/misc/strings/algorithm.hpp>
#include <components/settings/values.hpp>

namespace Weather
{
    osg::Vec3f defaultStormDirection()
    {
        return osg::Vec3f(0.f, 1.f, 0.f);
    }

    std::string_view stormEffect(std::string_view weather)
    {
        if (Misc::StringUtils::ciEqual(weather, "Ashstorm"))
            return Settings::models().mWeatherashcloud.get();
        if (Misc::StringUtils::ciEqual(weather, "Blight"))
            return Settings::models().mWeatherblightcloud.get();
        if (Misc::StringUtils::ciEqual(weather, "Snow"))
            return Settings::models().mWeathersnow.get();
        if (Misc::StringUtils::ciEqual(weather, "Blizzard"))
            return Settings::models().mWeatherblizzard.get();

        return std::string_view();
    }

    float windSpeed(std::string_view weather)
    {
        return Fallback::Map::getFloat("Weather_" + std::string(weather) + "_Wind_Speed");
    }

    Downpour downpourAt(std::string_view weather, float stormWindSpeed, float rainGravity)
    {
        const std::string name(weather);
        const float wind = windSpeed(weather);

        return Downpour{
            // **Morrowind hard-codes which weathers rain**, and the file it names is not the one the
            // engine draws: `Using_Precip` is the whole of the question, and the mesh below is the
            // answer upstream writes down for it.
            .mRainEffect = Fallback::Map::getBool("Weather_" + name + "_Using_Precip") ? "meshes\\raindrop.nif" : "",
            .mParticleEffect = std::string(stormEffect(weather)),
            .mPrecipitationAlpha = 1.f,
            .mRainSpeed = rainGravity,
            .mRainEntranceSpeed = Fallback::Map::getFloat("Weather_" + name + "_Rain_Entrance_Speed"),
            .mRainMaxRaindrops = Fallback::Map::getInt("Weather_" + name + "_Max_Raindrops"),
            .mRainDiameter = Fallback::Map::getFloat("Weather_" + name + "_Rain_Diameter"),
            .mRainMinHeight = Fallback::Map::getFloat("Weather_" + name + "_Rain_Height_Min"),
            .mRainMaxHeight = Fallback::Map::getFloat("Weather_" + name + "_Rain_Height_Max"),
            .mWindSpeed = wind,
            .mBaseWindSpeed = wind,
            .mIsStorm = wind > stormWindSpeed,
            .mStormDirection = defaultStormDirection(),
        };
    }
}
