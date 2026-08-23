#ifndef GAME_RENDER_SCENEFRAME_H
#define GAME_RENDER_SCENEFRAME_H

#include <osg/Vec3f>

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
    /// How the world is lit, as the game already knows it.
    ///
    /// **Read off the renderer rather than intercepted on its way in.** The sun, the ambient and the
    /// fog reach `RenderingManager` from four different places — the weather system, the cell's own
    /// `AMBI`, the night-eye effect, an interior's minimum brightness — and by the time they have
    /// settled into `mSunLight` and `FogManager` they have been through every one of those. Reading
    /// the settled values cannot disagree with what the rasterizer is drawing; catching the setters
    /// would have to reproduce the arithmetic between them.
    ///
    /// **Linear, and it takes a decode to get there.** Every colour here starts as a content file's
    /// three bytes and reaches `RenderingManager` as those bytes over 255 and nothing else:
    /// `SceneUtil::colourFromRGB` divides, `Fallback::Map::getColour` divides, and neither applies a
    /// transfer function. OpenMW works in that space from end to end and its own comment in
    /// `configureAmbient` calls it linear, which is true of its pipeline and not of the numbers —
    /// they are what an artist picked looking at a monitor. So `RtxBridge::decodeColour` runs where
    /// these are filled, exactly as it runs on the records the harness reads, and the two paths
    /// light the same world the same way.
    struct Lighting
    {
        /// The way the light travels, so a ray pointing back along it is pointing at the sun.
        osg::Vec3f mSunDirection;

        /// Scaled by `Shaders::DAYLIGHT`, which is the same ratio of sun to sky the harness uses.
        /// Sharing the constant is what keeps a screenshot and the game the same picture.
        osg::Vec3f mSunIrradiance;

        osg::Vec3f mAmbient;

        /// One colour, two uses: the horizon is fog seen from far enough away, which is why the game
        /// records a single value for both.
        osg::Vec3f mFog;

        /// What the sky is overhead, which is the one thing about it `mFog` cannot say.
        ///
        /// **It comes off `SkyManager` and nowhere else.** A weather's sky colour never reaches
        /// `RenderingManager`'s own state — the sky owns it and paints the dome with it — so this is
        /// a wire run to where it lives rather than a number recomputed from the weather. Equal to
        /// `mFog` where there is no sky to see, which is an interior: the dome is not drawn there and
        /// the colour the sky is still holding belongs to wherever the player was last outdoors.
        osg::Vec3f mSkyZenith;

        /// Per world unit. Derived from the linear ramp the rasterizer fogs with.
        float mFogExtinction = 0.0f;

        /// Negative infinity where the cell has none, which is how the shader spells "never".
        float mWaterLevel = 0.0f;
    };

    /// What there is to draw and what light is on it. No renderer appears in this type.
    ///
    /// **Handed down rather than reached up for.** A renderer that pulled the world would have to
    /// know `RenderingManager`, which sits above it; a renderer that is given one frame's worth of
    /// world knows only what a frame is. Where there is no world — the main menu, a loading screen,
    /// a video — there is no frame either, and `Renderer::renderGui` is what gets called instead.
    struct SceneFrame
    {
        /// The whole world, from the top. Not the cull's results: rays go everywhere, so anything a
        /// frustum would reject still has to be reachable.
        osg::Node& mScene;

        const osg::Camera& mCamera;

        /// Frame number and simulation time. The clock stops when the game is paused and so does
        /// everything the graph animates off it.
        const osg::FrameStamp& mWhen;

        const Lighting& mLighting;

        /// Where a texture the mirror has not seen before is read from.
        Resource::ImageManager& mImages;
    };
}

#endif
