#pragma once

#include <span>

namespace RtxTool
{
    /// One leg of a flight: the length of path between two keys, and whether the eye stands still
    /// at either end.
    struct CruiseLeg
    {
        double mLength = 0.0;
        bool mFromRest = false;
        bool mToRest = false;

        unsigned getRests() const { return (mFromRest ? 1u : 0u) + (mToRest ? 1u : 0u); }
    };

    /// The speeds a leg's two keys are passed at where one of them is not the leg's own: nought at
    /// an end that rests, and where the eye flies through a key, the speed of the slower of the two
    /// legs it joins.
    struct CruiseJoins
    {
        double mFrom = 0.0;
        double mTo = 0.0;
    };

    /// How a flight covers its legs: at one speed along the path, eased up from a rest and down to
    /// one over `mEase`. Any unit of time, so long as the legs' times are in it too.
    ///
    /// **The ease is a smoothstep of the speed**, `v·(3u² − 2u³)`, so the acceleration as well as
    /// the speed is nought where it begins and ends. It covers `v·E/2`, half what cruising for as
    /// long would, so a leg with `n` resting ends takes `L/v + n·E/2`; and where the leg is too
    /// short for its eases at `v`, they meet inside it and it takes `2L/v`. Both are
    /// `L/v + min(n·E/2, L/v)`, which is what every function here is the closed form of.
    struct Cruise
    {
        double mEase = 0.0;

        /// How long `leg` takes at `speed`.
        double timeFor(const CruiseLeg& leg, double speed) const;

        /// The speed `leg` flies at to take `time`.
        double speedFor(const CruiseLeg& leg, double time) const;

        /// The one speed at which every leg of `legs` takes `time` between them.
        ///
        /// **Exact, and not searched for.** The total is `A/v + B` between two of the speeds at
        /// which a leg's eases stop fitting, with `A` and `B` fixed by which side of its own each
        /// leg stands, so each interval has one candidate and the total's descent through them
        /// makes one of them the answer.
        double speedFor(std::span<const CruiseLeg> legs, double time) const;

        /// How far along `leg` the eye is `at` into it, where it takes `time`.
        double coveredAt(const CruiseLeg& leg, double time, double at) const;

        /// How far along `leg` the eye is `at` into it, where it takes `time` and passes its keys
        /// at `joins`' speeds.
        ///
        /// **The speed is changed within an ease of each key, by the same smoothstep**: from the
        /// join to a cruise and from the cruise to the next join, so the speed is continuous
        /// through a key and its rate nought on both sides of it. A leg too short for its two
        /// eases shares its time between them. The cruise is the one speed that covers the length
        /// in the time: `(L − j₀·e₀/2 − j₁·e₁/2) / (T − e₀/2 − e₁/2)`, which at a rest is the
        /// one-speed leg's. Joins no faster than the leg's own speed keep it positive.
        double coveredAt(const CruiseLeg& leg, double time, double at, const CruiseJoins& joins) const;
    };
}
