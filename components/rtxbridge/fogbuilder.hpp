#pragma once

#include <osg/Vec3f>

namespace ESM
{
    struct Cell;
}

namespace RtxBridge
{
    /// The air in a cell, in the units the shader takes.
    struct Fog
    {
        /// What the air scatters toward the eye, linear.
        osg::Vec3f mColour;

        /// How fast it swallows what is behind it, per world unit. Zero is a cell with no fog, and
        /// costs the shader nothing.
        float mExtinction = 0.0f;

        /// One where the air is an even haze rather than banked, which is what a room holds.
        float mUniform = 0.0f;
    };

    /// What a recorded fog depth comes to as an extinction coefficient.
    ///
    /// **The record is not a coefficient, and this fork can read what it actually is.** The original
    /// engine fogs *linearly* between two distances taken from the view range — `FogManager` writes
    /// it out as `start = view * (1 - depth)`, `end = view` — so a depth of 0.69 means "clear until
    /// two thousand units, gone by seven". A medium has no clear zone, so the two shapes cannot be
    /// made equal; what can be matched is where each is half gone.
    ///
    /// Half of the linear ramp is at `view * (1 - depth / 2)`, and an exponential is half gone at
    /// `ln(2) / sigma`, so
    ///
    ///     sigma = ln(2) / (view * (1 - depth / 2)).
    ///
    /// Clear weather's 0.69 over the game's own 7168 comes to 1.476e-4 — against the 1.5e-4 the
    /// renderer this is ported from settled by eye, which is two routes to the same number.
    ///
    /// **And it is the same conversion indoors**, because the original engine uses the same view
    /// range in both. A room is faint because it is small, not because its dial means something
    /// different — which is the answer a separate indoor scale was standing in for.
    float fogExtinction(float depth);

    /// An interior's own fog, out of its `AMBI` record. Only interiors carry one; an exterior's air
    /// belongs to the weather.
    Fog interiorFog(const ESM::Cell& cell);
}
