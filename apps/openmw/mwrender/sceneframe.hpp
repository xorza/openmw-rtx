#ifndef GAME_RENDER_SCENEFRAME_H
#define GAME_RENDER_SCENEFRAME_H

#include <optional>

#include "weatherresult.hpp"

#include <osg/Matrixf>
#include <osg/Vec3f>
#include <osg/Vec4f>

namespace osg
{
    class Camera;
    class FrameStamp;
    class Node;
}

namespace RtxBridge
{
    class Residency;
}

namespace Resource
{
    class ImageManager;
}

namespace MWRender
{
    /// What kind of place the player is standing in, as the cell record says.
    ///
    /// **Three and not two, because the two consumers split the middle one differently.** A
    /// quasi-exterior — Vivec's cantons, the Ministry of Truth — is an interior cell that draws a
    /// sky and has weather. The `isInterior` uniform counts it as inside, because that is what the
    /// cell is; the shader chain's exterior mask counts it as outside, because that is what it
    /// looks like. A single boolean could only have been right for one of them, and reading either
    /// off whether a dome happens to be drawn is a third answer again.
    enum class Location
    {
        Interior,
        QuasiExterior,
        Exterior,
    };

    /// A distance fog, as the game describes one: a colour and the linear ramp it fills.
    struct FogBand
    {
        osg::Vec4f mColour;
        float mStart = 0.0f;
        float mEnd = 0.0f;
    };

    /// What the world is doing this frame.
    ///
    /// **Read off where it settled rather than intercepted on the way in.** The sun, the ambient and
    /// the fog reach `RenderingManager` from four different places — the weather system, the cell's
    /// own `AMBI`, the night-eye effect, an interior's minimum brightness — and by the time they are
    /// on `mSunLight` and `FogManager` they have been through every one of those. Reading the
    /// settled values cannot disagree with what is drawn; catching the setters would have to
    /// reproduce the arithmetic between them.
    ///
    /// **In the world's own numbers, undecoded.** Every colour here is a content file's three bytes
    /// over 255 and nothing else: `SceneUtil::colourFromRGB` divides, `Fallback::Map::getColour`
    /// divides, and neither applies a transfer function. What that means is a question about a
    /// renderer's transport rather than about the world — the rasterizer's shader chain samples
    /// these as they are, and a renderer whose light transport is linear decodes them — so the
    /// conversion belongs to whoever is doing the converting.
    struct WorldState
    {
        /// Where the sun is drawn, which is not where its light comes from whenever
        /// `match sunlight to sun` is off.
        osg::Vec4f mSunPosition;

        /// The way the light travels, so a ray pointing back along it is pointing at the sun.
        osg::Vec4f mSunVector;

        bool mSunAtNight = false;
        osg::Vec4f mSunColour;
        float mSunVisibility = 0.0f;

        /// Includes the night-eye effect, because that is where it has already been added.
        osg::Vec4f mAmbientColour;

        /// Meaningless in an `Interior`, where the weather system stops writing it and it keeps
        /// whatever it held wherever the player was last outdoors. `mLocation` is what says so.
        osg::Vec4f mSkyColour;

        /// Where the player is standing, as the cell record says.
        ///
        /// **Asked of the world rather than worked out from what is drawn.** Reading it off the
        /// dome makes every quasi-exterior an exterior and hands `tsky` a say in it; reading it off
        /// whether terrain is enabled makes it a fact about the renderer's own bookkeeping.
        Location mLocation = Location::Interior;

        bool mWaterEnabled = false;
        float mWaterHeight = 0.0f;
        bool mUnderwater = false;

        /// Fog as it is right now, which under water is the water.
        FogBand mFog;

        /// Fog above the water, which is the air's own colour and how far it reaches. A renderer
        /// whose fog is a medium rather than a ramp reads this even with the eye submerged, because
        /// what it models down there is the water itself.
        FogBand mAir;

        float mNearClip = 0.0f;
        float mViewDistance = 0.0f;
        osg::Matrixf mProjectionMatrix;
        float mFieldOfView = 0.0f;

        float mGameHour = 0.0f;

        /// Which weather the sky is under, as a script id — an index into the ten
        /// `MWWorld::WeatherManager` registers.
        int mWeatherId = 0;

        /// Which one it is turning into, and nothing at all while it is turning into none.
        ///
        /// **The world says -1 there and this does not.** A sentinel inside the range of a field is
        /// the sort of thing a reader has to already know about, and a default of zero would have
        /// said "a transition to Clear, just finished" — which is a sky, and a wrong one.
        std::optional<int> mNextWeatherId;

        /// How far that transition has left to run, which is **one when it begins and zero when it
        /// ends**: `WeatherManager` counts it down, and its own mix is `1 - this`
        /// (`apps/openmw/mwworld/weather.cpp:1261`). Meaningless without `mNextWeatherId`.
        float mWeatherTransition = 0.0f;
        float mWindSpeed = 0.0f;

        /// Masser and Secunda, as the weather system last settled them.
        ///
        /// **The world's own numbers and not a placement**, which is what keeps this header off the
        /// ray tracer: `components/rtxbridge` is not built at all with the option off, and this is a
        /// header the rasterizer reads. An alpha of nothing is a moon that is not drawn, which is
        /// what a value-initialised pair says before the weather system has spoken.
        MoonState mMoons[2] = {};

        /// Where a storm drives what it carries, unit length.
        ///
        /// **Not derivable from the weather alone**, which is why it is reported rather than worked
        /// out downstream: an ash or blight storm blows off Red Mountain *at the player*, so the
        /// direction depends on where they stand. Every other weather leaves it due north.
        osg::Vec3f mStormDirection = osg::Vec3f(0.0f, 1.0f, 0.0f);

        /// Whether the cell record calls this an interior.
        ///
        /// **A quasi-exterior answers yes to this and to `isOutdoors` both**, which is the whole
        /// reason `Location` has three values and neither of these is the other's negation. This is
        /// the one the `isInterior` shader uniform has always meant: what the cell *is*.
        bool isInteriorCell() const { return mLocation != Location::Exterior; }

        /// Whether this counts as being outside — a sky overhead and weather in it.
        ///
        /// **A quasi-exterior answers yes to this and to `isInteriorCell` both.** It is the
        /// condition `World::updateWeather` gates on, so it is exactly when `mSkyColour` is being
        /// written and means something, and it is what a technique marked `Disable_Exteriors` is
        /// asking about.
        bool isOutdoors() const { return mLocation != Location::Interior; }
    };

    /// What there is to draw, and what the world is doing while it is drawn.
    ///
    /// **Handed down rather than reached up for.** A renderer that pulled the world would have to
    /// know `RenderingManager`, which sits above it; a renderer given one frame's worth of world
    /// knows only what a frame is. Where there is no world — the main menu, a loading screen, a
    /// video — there is no frame either, and `Renderer::renderGui` is what gets called instead.
    struct SceneFrame
    {
        /// The whole world, from the top. Not the cull's results: rays go everywhere, so anything a
        /// frustum would reject still has to be reachable.
        osg::Node& mScene;

        const osg::Camera& mCamera;

        /// Frame number and simulation time. The clock stops when the game is paused and so does
        /// everything the graph animates off it.
        const osg::FrameStamp& mWhen;

        const WorldState& mWorld;

        /// Where a texture the mirror has not seen before is read from.
        Resource::ImageManager& mImages;

        /// Geometry the graph does not parent, for a renderer that walks rather than culls.
        ///
        /// **`Terrain::QuadTreeWorld` resolves its chunks inside a cull and parents them to
        /// nothing**, so with `distant terrain` on the ground, the paged objects and the grass are
        /// invisible to any visitor that is not a cull. Asked rather than walked; null where the
        /// terrain parents its chunks like anything else, which is every other world.
        RtxBridge::Residency* mResident = nullptr;
    };
}

#endif
