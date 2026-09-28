#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace RtxTool
{
    /// The game's own `timescale`, the one a new game starts at and the clock keys of `sky.lua` halve
    /// and double: at it, a crossing takes the weather's own `Transition_Delta`.
    inline constexpr float sGameTimeScale = 30.0f;

    /// The sky as the harness crosses it, one weather into another, handed to the world whole on
    /// every frame (`MWBase::World::holdWeather`) where the harness has taken the sky.
    ///
    /// **Here and not the game's queue, because the queue answers a person too late.** The game
    /// runs one crossing and keeps one weather asked behind it, so a second ask during a crossing
    /// only replaced the one behind, and the sky showed nothing new for the minute the first took
    /// to land. Here an ask turns the crossing it lands on toward itself at once.
    ///
    /// Weathers are numbered as `Rtx::weatherIndex` numbers them.
    class SkyCrossing
    {
    public:
        /// Stands where the world stood when the harness took the sky: leaving `from` for `to`,
        /// `crossed` of the way, or standing at `from` where the two are one and at `to` where the
        /// world counts the crossing whole.
        SkyCrossing(std::uint32_t from, std::uint32_t to, float crossed);

        /// Turns the sky toward `weather` from what it shows now.
        ///
        /// **The weather that shows most keeps its share, and the other gives its share up.** The
        /// sky is `(1 - c)` of the one it leaves and `c` of the one it goes to, and the world mixes
        /// two and no more, so a third can only take one of the two places. Below halfway the
        /// crossing keeps its start and goes to `weather` from where it is; from halfway it starts
        /// again from the one it was going to, at `1 - c`. Either way at most half of the sky
        /// changes its weather on the press, and none of the precipitation, which the world swaps
        /// at halfway. An ask for the weather it leaves turns it round where it stands, which moves
        /// nothing at all.
        void ask(std::uint32_t weather);

        /// Stands at `weather` at once, crossing nothing: what an ask is under a stopped clock,
        /// which has no crossing to show.
        void settle(std::uint32_t weather);

        /// Moves the crossing on by `share` of a whole one, and stands at the weather it went to
        /// where it lands. Nothing where it crosses nothing.
        void advance(float share);

        /// The weather the sky leaves, or stands at.
        std::uint32_t getWeather() const { return mFrom; }

        /// The weather it goes to, and the one it stands at where it crosses nothing: the last one
        /// asked for, which the keys step from.
        std::uint32_t getNextWeather() const { return mTo; }

        /// How far the crossing has come, from nought up to but not including one.
        float getCrossed() const { return mCrossed; }

        bool isCrossing() const { return mFrom != mTo; }

        /// How much of a crossing `seconds` of the simulation's clock run for a weather whose own
        /// `Transition_Delta` is `delta`, under a game clock at `gameTimeScale`: the world's own
        /// crossing at the game's own speed, `sGameTimeScale`, and faster or slower as the clock
        /// is, so a sky crosses in step with the sun. Nought under a stopped clock.
        static float shareOf(float seconds, float delta, float gameTimeScale)
        {
            return seconds * delta * gameTimeScale / sGameTimeScale;
        }

        /// Where in `rolled` the weather `steps` on from `getNextWeather` stands, or back where
        /// negative, round and round. A weather `rolled` does not hold — what a save or the console
        /// can leave a region under — is before its first going on and after its last going back,
        /// so the first step lands on an end. `rolled` must hold one weather at least.
        std::size_t stepAmong(std::span<const std::uint32_t> rolled, int steps) const;

    private:
        std::uint32_t mFrom = 0;
        std::uint32_t mTo = 0;
        float mCrossed = 0.0f;
    };
}
