#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include <osg/Vec3f>

#include <components/rtx/scenedesc.hpp>

#include "fogbuilder.hpp"

namespace ESM
{
    struct Light;
    struct Region;
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

        /// Whether the sun's disc is up to be seen.
        ///
        /// **Not the same as whether it lights anything.** Its night colour is a dim blue and the
        /// engine reads that straight off the ramp, so the light never goes out; what goes out is
        /// the disc, between sunset and sunrise (`apps/openmw/mwworld/weather.cpp:651`).
        bool mSunVisible = false;

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

    /// A weather's index, as `MWWorld::WeatherManager` registers them and the shader's `WEATHER_*`
    /// name them, or nothing for a name that is none of the ten.
    ///
    /// **One table, two callers**, which is the point it shares with `makeLight`: the game hands the
    /// renderer a weather's script id and the harness hands it a name off a command line, and a
    /// frame taken either way has to be under the same sky.
    std::optional<std::uint32_t> weatherIndex(std::string_view weather);

    /// The name that index spells, for whoever has to hand one back to `makeDaylight`. Empty for
    /// an index past the ten.
    std::string_view weatherName(std::uint32_t weather);

    /// The weather one step either side of this one, skipping any the region never gets.
    ///
    /// **A region does not see all ten.** A `REGN` record carries ten chances that add to a hundred,
    /// in the order `WEATHER_*` names them, and a zero is a weather that never happens there: the
    /// ash wastes never snow, Solstheim never has an ashstorm, and offering either is offering a sky
    /// the game would not produce.
    ///
    /// A null region — an interior, or a cell whose record names none — offers all ten, and so does
    /// a region whose chances are all zero, since the alternative is a step that goes nowhere.
    std::uint32_t nextRegionWeather(const ESM::Region* region, std::uint32_t weather, bool forward);

    /// How hard the wind blows under a named weather, as the content files record it.
    ///
    /// The game interpolates between two of these; the harness, which runs no weather, reads the
    /// one. A name that is none of the ten throws, so ask `weatherIndex` first.
    float windSpeed(std::string_view weather);

    /// Where a storm drives what it carries, for an observer standing at `observer`.
    ///
    /// **Ash and blight blow off Red Mountain.** `apps/openmw/mwworld/weather.cpp:47` aims the
    /// direction from the volcano at whoever is standing in it, flattened to the ground — which is
    /// why an ashstorm comes at the player's face wherever they walk, and why this needs a position
    /// at all. Every other weather takes the wind's own bearing and does not.
    ///
    /// The game reports what its own weather system computed, since it has a player to ask about;
    /// this is the same rule for a harness that has only a camera.
    osg::Vec3f stormDirection(std::uint32_t weather, const osg::Vec3f& observer);

    /// The daylight a named weather casts at `hour`, on a twenty-four hour clock.
    ///
    /// @param weather a weather's name as the fallback settings spell it — "Clear", "Cloudy",
    ///        "Overcast" and the rest. **A name that is none of the ten throws** `std::logic_error`
    ///        out of the fallback map, which whitelists its keys one weather at a time; whoever
    ///        takes a name from outside should put it through `weatherIndex` before this.
    Daylight makeDaylight(std::string_view weather, float hour);

    /// The daylight partway between two weathers, at `blend` from the first to the second.
    ///
    /// **What `WeatherManager::calculateTransitionResult` does, and it blends the same things.** Each
    /// weather's numbers are read at the hour and then mixed — the fog's recorded *depth* among them
    /// rather than the extinction it becomes, because those are two different curves and the engine
    /// converts after blending.
    Daylight makeDaylight(std::string_view from, std::string_view to, float blend, float hour);

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
