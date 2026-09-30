#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Sky
{
    /// The `timescale` global as Morrowind ships it: thirty game seconds to each real one.
    constexpr float sVanillaTimeScale = 30.0f;

    /// How far the sky's own clock moves over `seconds` of simulation at `timeScale` game seconds to
    /// each: the one clock the deck's scroll and the fog's drift run on, beside the hour the sun and
    /// the stars already read. The weather manager's own crossing and thunder stay on the frame's
    /// clock, as upstream paces them: the game's pace is not this renderer's to change.
    ///
    /// **Real seconds at Morrowind's `timescale`, and a time-lapse at any other.** Morrowind paces
    /// its deck and its wind in real time and its sun in game time, so a sped-up clock raced the
    /// sun under a sky that stood still. Divided by the shipped scale rather than stated in game
    /// seconds, so `Cloud_Speed` and `FOG_GALE` keep the meaning they were authored with, and a run
    /// at the shipped `timescale` is the run it always was — the ratio first, so that at thirty the
    /// step is the simulation's own to the bit. A negative `timescale` holds the sky rather than
    /// running it backwards, as the engine's own hour stops at midnight under one.
    inline float skyStep(const float seconds, const float timeScale)
    {
        return seconds * (std::max(timeScale, 0.0f) / sVanillaTimeScale);
    }

    /// How far the star sphere has turned at `gameSeconds` of game time, in radians about the
    /// vertical, in [-π, π]: once round in four days, counter-clockwise as the player sees it, as
    /// Morrowind turns it. **Both renderers turn their stars by this one function**, the
    /// rasterizer's `SkyManager::update` and the ray tracer's `SkyReader`: an upstream change to
    /// the rule conflicts at the rasterizer's call, and is made here for both. Taken round the
    /// circle in double and only then narrowed, because a float's angle a hundred days in resolves
    /// a hundredth of a degree.
    inline float starRoll(const double gameSeconds)
    {
        constexpr double fourDays = 3600.0 * 96.0;
        return static_cast<float>(
            std::remainder(gameSeconds * (-2.0 * std::numbers::pi) / fourDays, 2.0 * std::numbers::pi));
    }

    /// How far a cloud deck scrolls over `seconds` of its renderer's clock, in texture units:
    /// `Cloud_Speed` over four hundred a second, and where the content sets
    /// `Weather_Timescale_Clouds`, by the game's `timescale` over sixty as well. **Both renderers
    /// scroll their decks by this one function**, the rasterizer's `SkyManager::update` and the ray
    /// tracer's `SkyClock`, each on its own clock: an upstream change to the rule conflicts at the
    /// rasterizer's call, and is made here for both.
    inline float cloudScrollStep(
        const float seconds, const float cloudSpeed, const float timeScale, const bool timescaleClouds)
    {
        float delta = seconds * cloudSpeed / 400.f;
        if (timescaleClouds)
            delta *= timeScale / 60.f;
        return delta;
    }

    /// Moves a deck's scroll on by `delta`, wrapped where the deck's sheet repeats: every four
    /// texture units.
    inline float scrollClouds(float scroll, const float delta)
    {
        scroll += delta;
        if (scroll >= 4.f)
            scroll -= 4.f;
        return scroll;
    }

    /// The clocks a renderer that draws no dome runs its sky on: how far the cloud deck has scrolled,
    /// in texture units, and how many of the sky's own seconds have passed. Neither is a function
    /// of the hour: the deck runs on the weather's speed. The rasterizer's `SkyManager` keeps its
    /// own deck at upstream's pace, and neither renderer reads the other's.
    struct SkyClock
    {
        /// A double, because the fog's drift is the difference of two readings, and ten hours in a
        /// float resolves 0.0039 s — a quarter of a frame at sixty.
        double mSeconds = 0.0;
        float mCloudScroll = 0.0f;

        /// Moves both clocks on by one frame of `seconds` at `timeScale`, under a deck driven at
        /// `cloudSpeed`: by the sky's clock, because `Cloud_Speed` is a rate over real seconds —
        /// unless the content set `Weather_Timescale_Clouds`, which paces the deck by the game's
        /// scale already, and is the frame's seconds under that rule as it is the rasterizer's.
        void step(const float seconds, const float timeScale, const float cloudSpeed, const bool timescaleClouds)
        {
            const float skySeconds = skyStep(seconds, timeScale);
            mSeconds += static_cast<double>(skySeconds);
            mCloudScroll = scrollClouds(mCloudScroll,
                cloudScrollStep(timescaleClouds ? seconds : skySeconds, cloudSpeed, timeScale, timescaleClouds));
        }
    };
}
