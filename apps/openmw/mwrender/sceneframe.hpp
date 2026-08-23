#ifndef GAME_RENDER_SCENEFRAME_H
#define GAME_RENDER_SCENEFRAME_H

#include <osg/Matrixf>
#include <osg/Vec4f>

namespace osg
{
    class Camera;
    class FrameStamp;
    class Node;
}

namespace Resource
{
    class ImageManager;
}

namespace MWRender
{
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

        osg::Vec4f mSkyColour;

        /// False in an interior, where no dome is drawn and `mSkyColour` is whatever the sky was
        /// still holding from wherever the player was last outdoors.
        ///
        /// **Not the same question as `mInterior`.** A quasi-exterior — Vivec's cantons, the
        /// Ministry of Truth — is an interior cell that draws a sky, so the two disagree there and
        /// each has a caller that wants its own answer.
        bool mSkyVisible = false;

        /// Whether the cell the player stands in is an interior, which is what the cell record
        /// says and nothing else.
        ///
        /// **Asked of the world rather than worked out from what is drawn.** Reading it off the
        /// sky makes every quasi-exterior an exterior, and reading it off whether terrain is
        /// enabled makes it a fact about the renderer's own bookkeeping.
        bool mInterior = false;

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
        int mWeatherId = 0;
        int mNextWeatherId = 0;
        float mWeatherTransition = 0.0f;
        float mWindSpeed = 0.0f;
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
    };
}

#endif
