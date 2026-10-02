#pragma once

#include <osg/Vec3f>

namespace Sky
{
    /// The full-screen wash the sun lays over the picture as the eye turns toward it, as the content
    /// sets it (`Weather_Sun_Glare_Fader_*`), read as the rasterizer's `SunGlareCallback` reads it,
    /// for the ray tracer's sky.
    struct SunGlareFader
    {
        /// `_Color` doubled and clamped to one. Replicating a design flaw in Morrowind, which set the
        /// colour on both the ambient and the emissive terms, multiplied by two, and let the fixed
        /// function pipeline clamp it: at the shipped values only the red clamps, so the wash is
        /// orange.
        osg::Vec3f mColour;

        /// `_Max`: the most of the wash there is, looking straight at the sun.
        float mMax;

        /// `_Angle_Max` in radians, past which there is none.
        float mAngleMax;

        /// The fader the fallback map sets.
        static SunGlareFader read();

        /// How much of `mMax` is left with the eye's axis `angle` radians from the sun:
        /// `1 - min(1, angle / mAngleMax)`.
        float atAngle(float angle) const;
    };
}
