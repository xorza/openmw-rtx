#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>

#include <osg/Matrixf>
#include <osg/Vec2f>
#include <osg/Vec3f>
#include <osg/Vec4f>

#include <components/esm3/loadcell.hpp>

namespace osg
{
    class FrameStamp;
    class Node;
}

namespace Terrain
{
    class ObjectStorage;
    class World;
}

namespace MWRender
{
    class Precipitation;
    struct SkyState;

    /// What kind of place the player is standing in, as the cell record says. Three and not two, because a
    /// quasi-exterior (Vivec's cantons) is an interior cell with a sky and weather: the shader chain's
    /// exterior mask counts it as outside, and `MWRender::SkyReader::read` as neither, as it has no ring of
    /// cut ground under it. The `isInterior` uniform reads `WorldState::mSkyShown` instead.
    enum class Location
    {
        Interior,
        QuasiExterior,
        Exterior,
    };

    /// The water as the game decides it: where it stands, whether the cell has any, and whether the
    /// `twf` command has hidden it. One rule for what the water covers, so the eye's answer and the
    /// rasterizer's `Water`, which is handed these numbers, cannot disagree.
    struct WaterState
    {
        float mHeight = 0.f;
        bool mEnabled = false;
        bool mToggled = true;

        bool isShown() const { return mEnabled && mToggled; }
        bool isUnderwater(const osg::Vec3f& position) const { return position.z() < mHeight && isShown(); }
    };

    /// A distance fog, as the game describes one: a colour and the linear ramp it fills.
    struct FogBand
    {
        osg::Vec4f mColour;
        float mStart = 0.0f;
        float mEnd = 0.0f;
    };

    /// What the game itself decides about the world this frame, read off where it keeps the value so it
    /// cannot disagree with what is drawn; the weather's is `SkyState`, carried beside this. In the world's
    /// own numbers, undecoded: every colour is a content file's three bytes over 255.
    struct WorldState
    {
        /// The sun light as `RenderingManager` keeps it, with `match sunlight to sun` already applied and, in
        /// a room, the position Morrowind points a room's sun. The rasterizer lights through the light itself;
        /// the chain and the tracer read these.
        osg::Vec4f mSunLightPosition;
        osg::Vec4f mSunColour;

        /// Includes the night-eye effect and, in a room, the lift `configureAmbient` gives it.
        /// `mRoom` is the record.
        osg::Vec4f mAmbientColour;

        /// The ambient before `updateAmbient` added the Night-Eye effect. Carried and not recovered:
        /// `mAmbientColour` less `mNightEye` misses it by an ulp past a doubling, and a colour stated as
        /// `k / 255` then decodes off the curve and not off the byte's table.
        osg::Vec4f mAmbientBeforeNightEye;

        /// What `updateAmbient` added to the ambient for the Night-Eye effect, in the file's space.
        /// Read back rather than restated, so there is one number. Nought without the effect.
        osg::Vec4f mNightEye;

        /// `RenderingManager::setSunColour`'s third argument: the one number the light does not
        /// hold. Nought in a room.
        float mSunVisibility = 0.0f;

        /// Whether there is a sky to draw: outdoors, and `tsky` has not turned it off. What
        /// `RenderingManager::setSkyEnabled` was last told.
        bool mSkyShown = false;

        /// Whether a script has painted Secunda red, `Moons_Script_Color`.
        /// `RenderingManager::skySetMoonColour`.
        bool mMoonRed = false;

        /// Where the player is standing, as the cell record says and not as read off what is drawn:
        /// off the dome every quasi-exterior is an exterior and `tsky` has a say in it.
        Location mLocation = Location::Interior;

        WaterState mWater;

        /// Whether the eye is under `mWater`: the water's own rule, asked at the camera's position as upstream
        /// asks it. The ray tracer takes its medium from the eye it traces.
        bool mUnderwater = false;

        /// Fog above the water, which a renderer whose fog is a medium reads even with the eye
        /// submerged, because what it models down there is the water itself.
        FogBand mAir;

        /// Fog with the eye under the water: the water. Both bands every frame, because the
        /// rasterizer's shaders carry both and switch per fragment.
        FogBand mWaterFog;

        /// Where the player stands, which the rasterizer's vegetation bends away from.
        osg::Vec3f mPlayerPosition;

        /// The `AMBI` record of the room the player is in, or nothing elsewhere. The record and not
        /// `mAmbientColour`, because `configureAmbient` lifts an interior's ambient to `minimum interior
        /// brightness` and turns its sunlight directional. Only in an `Interior`: a quasi-exterior's light
        /// is the weather's.
        std::optional<ESM::Cell::AMBIstruct> mRoom;

        float mGameHour = 0.0f;

        /// Which child every `NightDaySwitch` of the world's objects shows, `World::getNightDayMode`,
        /// or nothing where `day night switches` is off and each shows the child its file opens on.
        std::optional<unsigned int> mNightDayMode;

        /// Game seconds since the game began, `MWWorld::DateTimeManager::getGameTime`: the stars are turned by
        /// the clock and not a sum of frames, so a clock set or run forward turns them with it.
        double mGameTime = 0.0;

        /// The `timescale` global: game seconds to each real one, which a renderer steps its own
        /// sky clock by (`Sky::SkyClock`).
        float mTimeScale = 0.0f;

        /// Which weather the sky is under, as a script id: an index into the ten
        /// `MWWorld::WeatherManager` registers.
        int mWeatherId = 0;

        /// Which one it is turning into, and nothing while it is turning into none: the world says -1 there,
        /// and a default of zero would say "a transition to Clear, just finished". The two fields below are
        /// read unconditionally and hold whatever the weather manager last left.
        std::optional<int> mNextWeatherId;

        /// How far that transition has left to run: one when it begins and zero when it ends, the
        /// weather manager's own mix being `1 - this`. Meaningless without `mNextWeatherId`.
        float mWeatherTransition = 0.0f;

        /// How hard the wind blows, as the game's own dial: what the rasterizer's `windSpeed`
        /// uniform leans its vegetation by. The gust, and not the base the weather records, which
        /// is `SkyState::mWeather`'s.
        float mWindSpeed = 0.0f;

        /// Whether this counts as being outside — a sky overhead and weather in it: the condition
        /// `World::updateWeather` gates on, and what a technique marked `Disable_Exteriors` asks.
        bool isOutdoors() const { return mLocation != Location::Interior; }
    };

    /// Where the sun's disc is drawn: the weather's orbit where the weather ran, and in a room the point
    /// `configureAmbient` aimed the light, with no night. One rule, because the shader chain and the trace
    /// both place a disc.
    osg::Vec4f sunDiscOf(const SkyState& sky, const WorldState& world);

    /// Where the frame is seen from, and how far it can see: the eye's, not the world's, because the same
    /// world is drawn through several. Where the eye stands is the camera's own view matrix, which the update
    /// traversal writes after this is described: a renderer reads it off the camera it adopted when it draws.
    struct EyeState
    {
        float mNearClip = 0.0f;
        float mViewDistance = 0.0f;

        /// How far a script asked the player to see, `camera.setViewDistance`, where that is not the setting.
        /// The ray tracer closes its air here and keeps the world standing to the setting's reach, so a script
        /// never rebuilds the world.
        std::optional<float> mScriptViewDistance;

        /// Where the air closes for a world that stands to `reach`: there, or nearer where a script
        /// asked the eye to see less.
        float closesAirAt(float reach) const { return std::min(reach, mScriptViewDistance.value_or(reach)); }

        osg::Matrixf mProjectionMatrix{};

        /// A script's `camera.setProjectionOffset`, as the translation `mProjectionMatrix` carries after its
        /// perspective: clip units, x right and y up. Said apart for a renderer that builds its own projection
        /// at its traced extent, so the picture moves by the same fraction of itself.
        osg::Vec2f mProjectionShift{};

        /// The one the world settled on: the override wherever something asked for one, and the
        /// setting only where nothing did.
        float mFieldOfView = 0.0f;

        /// The one the player's own arms are drawn through, `first person field of view`, which
        /// `NpcAnimation` swaps the projection to under `Mask_FirstPerson`.
        float mArmsFieldOfView = 0.0f;
    };

    /// What there is to draw, and what the world is doing while it is drawn. Handed down rather than reached
    /// up for; where there is no world, `Renderer::renderGui` is called instead.
    struct SceneFrame
    {
        /// The whole world, from the top. Not the cull's results: rays go everywhere.
        osg::Node& mScene;

        /// Frame number and simulation time. The clock stops when the game is paused and so does
        /// everything the graph animates off it.
        const osg::FrameStamp& mWhen;

        /// What the weather settled, the weather manager's own.
        const SkyState& mSky;

        /// What the weather drops, as the game's particle systems: both renderers walk them, each asking what
        /// it needs.
        const Precipitation& mPrecipitation;

        const WorldState& mWorld;

        const EyeState& mEye;

        /// The world's terrain, for its storage, its worldspace and the active grid — not for its
        /// chunks, which a renderer that stands the ground itself is given none of.
        Terrain::World& mTerrain;

        /// What the content files say stands where: the ground's statics and the lights of the
        /// cells the paging leaves dark, which `Rtx::CellRing` reads out of here because the paging
        /// stands no `LIGH`.
        const Terrain::ObjectStorage& mObjectStorage;

        /// How long the frame stands for, in seconds, and whether the simulation stood still over
        /// it: what `RenderingManager::update` was handed, for the objects that step by it.
        float mDeltaTime = 0.0f;
        bool mPaused = false;

        /// The stable identities of the references the game put somewhere else in one step since the last
        /// frame (a door, a teleport, a script's `Position`), whose history is no history of where they now
        /// stand (`SceneUtil::StableIdentity`). Ascending, each once.
        std::span<const std::uint64_t> mJumped{};
    };
}
