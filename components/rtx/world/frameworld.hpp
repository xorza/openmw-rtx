#pragma once

#include <array>
#include <limits>
#include <optional>

#include <osg/Vec2d>
#include <osg/Vec2f>
#include <osg/Vec3f>

#include <components/rtx/shaders/scene.h>
#include <components/rtx/shaders/sky.h>
#include <components/rtx/shaders/visibility.h>
#include <components/sky/skyclock.hpp>

#include "moon.hpp"
#include "skycontent.hpp"
#include "skylight.hpp"
#include "sunglare.hpp"

namespace Rtx
{
    /// What a frame's sky, air and water are, as far as neither host can work it out for the other:
    /// a `Daylight` and the handful of things a `Daylight` does not carry.
    struct WorldReading
    {
        Daylight mDaylight;

        /// Whether there is a sky over this cell at all. Nothing under `false` draws a deck, a star,
        /// a patch or a moon, or lights its air by a dome. True of a quasi-exterior, which is an
        /// interior cell the engine runs the weather system for.
        bool mOutdoors = false;

        /// Whether that sky is drawn, which `tsky` turns off: the eye and the mirrors then see the
        /// fog colour where they reached nothing, and the sky lights all it lit.
        bool mSkyDrawn = true;

        /// The weather's `Glare_View`, which is what keeps the stars in under an overcast.
        float mGlare = 1.0f;

        /// How far the star sphere has turned, `Sky::starRoll` of the game's clock.
        float mStarRoll = 0.0f;

        /// Masser and Secunda, placed and with their faces named. An input and not a derivation:
        /// the angles come from the weather system, and nothing here can work them out.
        std::array<MoonPlacement, 2> mMoons{};

        /// Which weather is over the eye and which is arriving, and how the two decks stand.
        CloudCrossing mClouds;

        float mWaterLevel = -std::numeric_limits<float>::infinity();

        /// The colour the content settles its water's murk at, which the water scatters back:
        /// `Shaders::VisibilityConstants::mWaterScatter`.
        osg::Vec3f mWaterScatter = Shaders::WATER_SCATTER_SHIPPED;

        /// What the water moves by, and what the sky does: the simulation's seconds and the sky's own
        /// clock, two because a sped-up sky is a time-lapse and sped-up water is noise. Both double, since
        /// a float holding ten hours resolves a quarter of a frame; the consumer narrows them.
        double mSeconds = 0.0;
        double mSkySeconds = 0.0;

        float mRainOnWater = 0.0f;

        /// How far over the eye a roof keeps what is falling off, or nought where nothing falling
        /// is kept off — `Shaders::VisibilityConstants::mShelterHeight`, which is the game's
        /// occluder box and not a number decided here.
        float mShelterHeight = 0.0f;

        /// The sun glare fader as the game states it this frame, which the frame's options carry
        /// to the display chain.
        SunGlare mSunGlare;

        /// What Night-Eye adds to the ambient, in the engine's colour values. **Never a light of the world**:
        /// nothing occludes it and no exposure adapts it away, so the display chain adds it after the meter.
        osg::Vec3f mNightEye{};
    };

    /// What a frame's world decides beside its block: what the display chain washes and lifts the
    /// picture by, and the sea's clock. Written by `describeWorld` and read by the renderer, and
    /// never stated by a host, which hands the world (`WorldReading`) and not what it comes to.
    struct WorldOptions
    {
        /// What to multiply the measured exposure by: the hour, which the histogram cannot see
        /// (`Rtx::Skylight::mExposureBias`). A fixed exposure is not touched by it.
        float mExposureBias = 1.0f;

        /// The sun glare fader over the picture, which the display chain washes it with. None for a
        /// frame no world was described over.
        SunGlare mGlare{};

        /// What Night-Eye adds to the ambient, in the engine's colour values, which the display
        /// chain lays over the picture after the curve (`ToneConstants::mNightEye`). None for a
        /// frame no world was described over.
        osg::Vec3f mNightEye{};

        /// The water's clock in seconds, as the host keeps it, which the wake steps by: whole here, where
        /// the frame block carries it split (`VisibilityConstants::mWaterTime`), so a step boundary is
        /// read off the clock and not off a rebuilt split.
        double mWaterSeconds = 0.0;
    };

    /// How far the air has been carried downwind since a run began, in world units: the integral of the
    /// wind over the sky's clock, kept across frames by whoever traces them. A wind times the clock is not
    /// a displacement: it jumps by the whole clock's worth of the difference whenever the wind changes
    /// (ten minutes in, clear's 0.1 becoming a storm's 0.5 moved the field 336,000 units in 67 seconds).
    /// Stepped by what the clock moved, so a change of wind changes the speed and nothing else.
    class FogDrift
    {
    public:
        /// Carries the air on by what the sky's clock moved since the last call, along `heading` at
        /// `wind` — `Fog::mWind`, in the units `FOG_GALE` converts. The first call moves nothing,
        /// and the clock never runs backwards: `Sky::skyStep` holds it under a negative
        /// `timescale`, and the host only ever adds to it.
        void advance(const osg::Vec2f& heading, float wind, double seconds);

        const osg::Vec2d& get() const { return mCarried; }

        /// Stands the air at `carried` as of `seconds` of the sky's clock, so the next `advance` at
        /// that clock moves nothing and the one after carries it on from there.
        void hold(const osg::Vec2d& carried, double seconds);

    private:
        /// In double, because it grows without bound: after ten hours of storm a float's step
        /// across it is two units against the twelve a frame carries it.
        osg::Vec2d mCarried;
        std::optional<double> mLastSeconds;
    };

    /// Where the clocks the air runs on stand: the sky's own, which the deck scrolls by and the fog churns
    /// by, and how far `FogDrift` has carried the fog. What a frame's air is a function of beside the
    /// weather and the hour, so a moment can be stood in again. Neither is derived from the other: every
    /// change of weather on the way changed the wind.
    struct AirClock
    {
        Sky::SkyClock mSky;
        osg::Vec2d mCarried{};
    };

    /// `seconds` as two floats whose sum is it, for a shader to reduce exactly (`turnsAt`): the
    /// nearest float, and the nearest float to what that left over. What they carry together is
    /// good to a nanosecond after years, where one float resolves a quarter of a frame after ten
    /// hours.
    osg::Vec2f splitSeconds(double seconds);

    /// Where each scale of the fog's field is read from, `Shaders::VisibilityConstants::mFogOffsets`:
    /// the churn over `skySeconds` and the air `carried` downwind, turned as the scale is turned,
    /// reduced against the scale's tile in double and handed over as a fraction of it.
    std::array<osg::Vec3f, Shaders::FOG_SCALES> fogOffsets(const osg::Vec2d& carried, double skySeconds);

    /// Writes the frame's world half into the constants it is traced with, and into `options` what rides
    /// beside them: the exposure's bias, the glare fader, Night-Eye's lift and the sea's clock. The
    /// camera's half (`constantsFor`) is left alone. The order is the whole of what this is for: the stars
    /// before the sky's budget, the budget before the air, and both before the deck. `drift` is stepped
    /// here, because the heading it blows along is the deck's, which is settled here and nowhere else.
    ///
    /// @param sky where the sky's own sheets sit in the scene's texture table: made at load
    ///        (`addSkyContent`) and borrowed by every frame, never copied into one.
    void describeWorld(const WorldReading& reading, const SkyContent& sky, FogDrift& drift,
        Shaders::VisibilityConstants& constants, WorldOptions& options);
}
