#pragma once

#include <optional>

#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>

#include "fogbuilder.hpp"

namespace ESM
{
    struct Light;
}

namespace RtxBridge
{
    /// The light a `LIGH` reference casts, or nothing where it casts none.
    ///
    /// Three kinds of record place no light. A **carried** one is a torch in a pack, lit only when
    /// something equips it. An **off by default** one does not burn while it sits in a cell. And a
    /// **negative** one *subtracts* illumination — a trick available to a renderer accumulating into
    /// a framebuffer and meaningless to anything that traces a ray to an emitter.
    std::optional<Rtx::Light> makeLight(const ESM::Light& record, const osg::Vec3f& position);

    /// The same light, from a colour and a radius rather than from a record.
    ///
    /// **One conversion and two callers**, which is the point: the harness reads a cell's `LIGH`
    /// records and the game reads the `SceneUtil::LightSource` nodes its own scene graph already
    /// holds, and the two must not come to disagree about how bright a candle is.
    ///
    /// @param colour linear, as the game's own lighting already is.
    /// @param radius the recorded one. Null where it is not a size a light can have.
    std::optional<Rtx::Light> makeLight(const osg::Vec3f& colour, float radius, const osg::Vec3f& position);

    /// The sun and the sky at one hour, as the content files describe them.
    ///
    /// Every colour here is a fallback setting the game reads for itself, and the sun's path is the
    /// arithmetic at `apps/openmw/mwworld/weather.cpp:901`. What is missing is the cross-fade
    /// `MWWorld::Weather` runs through sunrise and sunset: this steps between the four phases where
    /// the game ramps, which is exact at every hour outside a transition window and is as much as
    /// something with no weather simulation can honestly claim. The ramp arrives with the engine.
    struct Daylight
    {
        Rtx::Sun mSun;

        /// Sky radiance, linear, at the horizon and overhead.
        osg::Vec3f mSkyHorizon;
        osg::Vec3f mSkyZenith;

        /// What an exterior gets in place of a cell's `AMBI`, which only interiors carry.
        osg::Vec3f mAmbient;

        /// The weather's own air.
        ///
        /// **Its colour is `mSkyHorizon`, and the same read fills both.** Morrowind records one
        /// colour for the fog and for the sky's lower half because they are the same thing at two
        /// distances — the horizon *is* fog — so a ray that reaches nothing and a ray through a mile
        /// of air have to arrive at the same answer.
        Fog mFog;
    };

    /// Which of the four sets of colours a weather is read at.
    enum class SkyPhase
    {
        Night,
        Sunrise,
        Day,
        Sunset,
    };

    /// The phase `hour` falls in, between the hours the day runs from and to.
    SkyPhase phaseAt(float hour, float sunrise, float nightStart);

    /// The unit vector the sun's light travels *along* at `hour`.
    ///
    /// `apps/openmw/mwworld/weather.cpp:901`'s own arithmetic: the sun crosses from one horizon to
    /// the other over the day and back under the world over the night, along a fixed arc. Its height
    /// does not change between the two, and neither does the engine's — a night is dark because the
    /// sun stops shining, not because it goes below the ground.
    osg::Vec3f sunDirection(float hour, float sunrise, float nightStart);

    /// The daylight a named weather casts at `hour`, on a twenty-four hour clock.
    ///
    /// @param weather a weather's name as the fallback settings spell it — "Clear", "Cloudy",
    ///        "Overcast" and the rest. An unknown one reads as a moonless night rather than
    ///        throwing: the settings come off a file this does not own.
    Daylight makeDaylight(std::string_view weather, float hour);

    /// A colour as the content files store one, decoded.
    ///
    /// Morrowind's colours are display-encoded, and the light transport downstream is linear. The
    /// two differ most in the middle, so mid grey is where a renderer that skips this is most
    /// obviously wrong and where a test pins it.
    osg::Vec3f decodeColour(std::uint32_t packed);

    /// The same decode, for a colour something else has already unpacked to `[0, 1]`.
    ///
    /// **What the game hands over is display-encoded too.** OpenMW's own renderer works in that
    /// space from end to end and never converts, so every colour read off a light, a fog or the sky
    /// is the file's own number divided by 255 — and a ray tracer that took it as linear would be
    /// as wrong there as it would be reading the record itself. The alpha is dropped: nothing
    /// downstream has a use for it.
    osg::Vec3f decodeColour(const osg::Vec4f& encoded);
}
