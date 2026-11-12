#pragma once

#include <osg/Vec3f>

namespace Rtx
{
    /// The air in a cell, in the units the shader takes.
    struct Fog
    {
        /// What the air scatters toward the eye, linear: out of doors `fogColour` of the dome's
        /// mean, and a room's record as it is.
        osg::Vec3f mColour;

        /// How fast it swallows what is behind it, per world unit. Zero is a cell with no fog.
        float mExtinction = 0.0f;

        /// How much of the air is an even haze rather than banked: all of it in a room, and
        /// `Shaders::FOG_EXTERIOR_EVEN` of it out of doors.
        float mUniform = 0.0f;

        /// How deep the layer stands, against the bank clear weather makes in dead still air
        /// (`Shaders::VisibilityConstants::mFogLift`).
        float mLift = 1.0f;

        /// What the weather records blowing at, which carries the field downwind; the heading is
        /// the cloud deck's, and `Rtx::FogDrift` is how far it has been carried.
        float mWind = 0.0f;

        /// How far the world is built, in units, and so where the air becomes opaque. Zero is a
        /// cell with nothing cut off, which is what a room is.
        float mEdge = 0.0f;
    };

    /// What a recorded fog depth comes to as an extinction coefficient. The original engine fogs
    /// linearly from `view * (1 - depth)` to `view`, and a medium has no clear zone, so the two are
    /// matched where each is half gone: `sigma = ln(2) / (view * (1 - depth / 2))`. Clear weather's
    /// 0.69 over the game's 7168 comes to 1.476e-4. `over` is the distance the half-life is measured
    /// across (`CellGrid::reachOf` out of doors, `sInteriorFogReach` in a room), a parameter and never a
    /// setting, because air tuned short of the world swallows the ground beyond.
    ///
    /// **Past `deepestHalfPointDepth`, the mean visibility over the view and not the half point.** The
    /// half point nears the eye as the depth nears two (from two it is behind the eye and gives the
    /// densest air there is, a screen of fog colour in Blizzard), and a medium cannot fog the eye as the
    /// ramp does, so the two are matched over the whole view instead:
    /// `(1 - e^-(sigma view)) / (sigma view) = 1 / (2 depth)`.
    float fogExtinction(float depth, float over);

    /// The depth at which the two rules of `fogExtinction` give one extinction, between one and
    /// two, 1.514: the half point up to it, and the mean over the view beyond, so the air is
    /// continuous across it and every weather Morrowind ships but Blizzard reads the half point.
    float deepestHalfPointDepth();

    /// What clear weather records its own land fog depth as, which every other weather's layer is
    /// a ratio of. Morrowind ships 0.69, day and night alike.
    constexpr float sClearFogDepth = 0.69f;

    /// What a wind of one adds to the layer's depth: turbulence stands a storm up out of a bank.
    /// Morrowind's recorded speeds run to 0.9, so this reaches 4.6 times the still layer; wind cannot be
    /// the whole of the lift, because the weather named foggy has a wind of nought.
    constexpr float sFogWindLift = 4.0f;

    /// How deep a weather's layer stands, as a multiple of the one `FOG_HEIGHT` names. Positive
    /// for every record: a depth `fogExtinction` reads as no fog stands the clear layer, and a wind
    /// not above nought lifts nothing.
    float fogLift(float depth, float wind);

    /// What the air scatters toward the eye: as bright as the sky's own light (`skyMean`, a radiance)
    /// and in the weather's recorded colour (`hue`).
    ///
    /// **The sky's luminance and not its colour.** The record is the horizon the dome is drawn with, so
    /// its colour already holds the sky's: lit by the dome's own colour as well, the blue was counted
    /// twice.
    ///
    /// **Normalised by the brightest channel and not by luminance**, because a scattering albedo
    /// cannot exceed one: no channel of the air is brighter than the light that lit it.
    osg::Vec3f fogColour(const osg::Vec3f& skyMean, const osg::Vec3f& hue);

    /// The distance a room's air is measured over: the view range's shipped default, stretched by one
    /// factor set by eye. A constant, because measuring against `viewing distance` thinned every cellar
    /// as it was raised. The original's ramp is clear beyond any wall in a room; unstretched, a medium
    /// would lift the room's black level by a fifth.
    constexpr float sInteriorFogReach = 25.0f * 7168.0f;

    /// The open air, from the colour and the fog depth a weather is at. One place decides what
    /// `reach` (`CellGrid::reachOf`) means: the extinction is a half-life over it, the edge closes
    /// at it, and only a landscape is large enough to bank.
    Fog exteriorFog(const osg::Vec3f& colour, float depth, float wind, float reach);

    /// The same air over a cell that is built whole — Vivec's cantons, Mournhold — where the edge
    /// would close over nothing and so is not there.
    Fog quasiExteriorFog(const osg::Vec3f& colour, float depth, float wind, float reach);

    /// A room's air: measured over `sInteriorFogReach` and closing over nothing.
    Fog roomFog(const osg::Vec3f& colour, float depth);
}
