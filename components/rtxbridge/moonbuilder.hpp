#pragma once

#include <osg/Vec3f>

namespace RtxBridge
{
    /// Which of the two moons over Vvardenfell.
    enum class Moon
    {
        Masser,
        Secunda,
    };

    /// Where a moon stands, how big it is, and how much of it the sun has.
    ///
    /// **A disc rather than a body.** A ray that reaches nothing finds the moon the way it finds the
    /// sun — there is no sphere in any acceleration structure — so what a moon is here is a
    /// direction, an angular size, and the two axes its face is painted along.
    struct MoonPlacement
    {
        /// Unit vector toward the moon.
        osg::Vec3f mDirection;

        /// The face's own axes, unit and perpendicular to `mDirection` and to each other: `mRight`
        /// runs along the portrait's `u` and `mUp` against its `v`.
        ///
        /// **A moon is not a billboard.** It keeps its face toward the world and its orientation
        /// toward its own arc, so the portrait turns against the horizon as the moon crosses — which
        /// is what `Moon::setState` builds out of the same two rotations and what the painted maria
        /// need if they are not to slide.
        osg::Vec3f mRight;
        osg::Vec3f mUp;

        /// Half the angle the disc subtends, in radians.
        float mAngularRadius = 0.0f;

        /// How far round its cycle the moon is, in radians: **zero is full and pi is new**.
        ///
        /// The lit share of the face is `(1 + cos) / 2`, and the sign of the sine says which limb
        /// keeps it — so waxing and waning are one number rather than a flag beside it. The game
        /// ships eight painted phases and this is the angle each of them stands for.
        float mPhaseAngle = 0.0f;

        /// What the game fades the moon out by near the horizon and around the ends of its arc, from
        /// zero to one. Zero is a moon that is not there to be drawn.
        float mAlpha = 0.0f;
    };

    /// Where a moon stands on `day` at `hour`, out of the `Moons_*` settings.
    ///
    /// **`MWWorld::MoonModel`'s own arithmetic, reached from the clock rather than from the weather
    /// system** — `apps/openmw/mwworld/weather.cpp:363` is where it was reverse engineered and where
    /// every one of the odd-looking constants is explained. It is here rather than borrowed because
    /// the game must keep working with the ray tracer compiled out, so the two exist and the tests
    /// below are what keep them saying the same thing.
    ///
    /// @param day days since the world began, on Morrowind's own count: the game starts on day 0,
    ///        which the rise-hour formula anchors to 16 Last Seed.
    /// @param hour on a twenty-four hour clock.
    MoonPlacement makeMoon(Moon moon, int day, float hour);

    /// The angular radius `makeMoon` gives that moon, in radians.
    ///
    /// **Out of the renderer the game already has**, and not out of the mesh: `Moons_<name>_Size` is
    /// scaled by 450/125 onto a quad of half-extent 0.5 a thousand units off
    /// (`apps/openmw/mwrender/gl/skyutil.cpp:641`), so the disc is `atan(1.8 * size / 1000)` across
    /// its radius. Masser's 94 comes to 9.6 degrees and Secunda's 40 to 4.1 — a moon nineteen
    /// degrees wide, which is the sky Morrowind is remembered for and thirty-five times the sun.
    float moonAngularRadius(Moon moon);
}
