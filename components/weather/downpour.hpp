#pragma once

#include <string>

#include <osg/Vec3f>

#include <components/vfs/pathutil.hpp>

namespace Weather
{
    /// Which way a storm blows before anything has turned it: due north, which is where every
    /// weather that names no direction of its own leaves it.
    osg::Vec3f defaultStormDirection();

    /// What a weather drops, how hard, and how fast the wind drives it.
    ///
    /// **The whole of what `Precipitation` needs and nothing else.** The game works these out with
    /// transitions and hands them over inside a much larger answer about the sky; the harness reads
    /// them straight off the content files with `downpourAt`. Naming them once, here, is what lets
    /// both build the same rain — and what keeps a class that owns particle systems from having to
    /// include the game's weather manager to be told it is raining.
    struct Downpour
    {
        /// The mesh the rain is, or empty where this weather drops none. Morrowind names one file
        /// and the engine ignores it, building a particle system instead; what it means is "rain".
        std::string mRainEffect;

        /// The model a storm drives past the eye — snow, a blizzard, ash, blight — or empty.
        std::string mParticleEffect;

        /// How much of it has arrived, which is what a transition fades.
        float mPrecipitationAlpha = 0.f;

        float mRainSpeed = 0.f;
        float mRainEntranceSpeed = 1.f;
        int mRainMaxRaindrops = 0;
        float mRainDiameter = 0.f;
        float mRainMinHeight = 0.f;
        float mRainMaxHeight = 0.f;

        /// What the wind is doing now, and what this weather's own is before the transition mixes
        /// it. The first leans the drops; the second is what the world asks about.
        float mWindSpeed = 0.f;
        float mBaseWindSpeed = 0.f;

        /// Whether this weather blows hard enough to turn its effect to face the wind.
        ///
        /// Which way it is driving is not here: that is `Precipitation::setStormDirection`, because
        /// the game turns it as the transition runs rather than settling it with the weather.
        bool mIsStorm = false;
    };

    /// What a named weather drives past the eye — snow, a blizzard, ash, blight — or empty where it
    /// drives nothing.
    ///
    /// **Morrowind hard-codes this and the content files do not record it**, so it is a table; the
    /// point of the table being here is that there is one of it. The game's weather manager and a
    /// harness with no weather system read the same four entries.
    std::string_view stormEffect(std::string_view weather);

    /// How hard a named weather blows, as the content files record it.
    ///
    /// **A name that is none of the ten throws**, because `Fallback::Map` will not consider a key it
    /// does not whitelist — which is why a caller holding a name from outside asks
    /// `RtxBridge::weatherIndex` first.
    float windSpeed(std::string_view weather);

    /// What a named weather drops, read straight off the content files.
    ///
    /// **The settled weather and not a moment inside a transition.** Every number here is the one
    /// the file records, which is what a weather arrives at; the game mixes the same set across a
    /// crossing and a harness that renders one instant wants the destination.
    ///
    /// @param stormWindSpeed `fStromWindSpeed`, above which a weather counts as a storm — a game
    ///        setting rather than a fallback, so it comes from outside.
    /// @param rainGravity `Weather_Precip_Gravity`, which every weather's rain falls at.
    Downpour downpourAt(std::string_view weather, float stormWindSpeed, float rainGravity);
}
