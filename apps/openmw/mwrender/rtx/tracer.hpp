#ifndef GAME_RENDER_RTX_TRACER_H
#define GAME_RENDER_RTX_TRACER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <osg/Vec3f>
#include <osg/ref_ptr>

#include <components/rtx/scenedesc.hpp>
#include <components/rtx/upscale.hpp>

namespace osg
{
    class Camera;
    class Node;
}

namespace Resource
{
    class ImageManager;
}

namespace Rtx
{
    class Renderer;
}

namespace RtxBridge
{
    class SceneExtractor;
}

namespace MWRender::Rtx
{
    class Composite;

    /// How the world is lit, as the game already knows it.
    ///
    /// **Read off the renderer rather than intercepted on its way in.** The sun, the ambient and the
    /// fog reach `RenderingManager` from four different places — the weather system, the cell's own
    /// `AMBI`, the night-eye effect, an interior's minimum brightness — and by the time they have
    /// settled into `mSunLight` and `FogManager` they have been through every one of those. Reading
    /// the settled values cannot disagree with what the rasterizer is drawing; catching the setters
    /// would have to reproduce the arithmetic between them.
    ///
    /// Linear, because OpenMW's own lighting already is: `configureAmbient` says so where it
    /// computes a relative luminance without converting first.
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

        /// Per world unit. Derived from the linear ramp the rasterizer fogs with — see the tracer.
        float mFogExtinction = 0.0f;

        /// Negative infinity where the cell has none, which is how the shader spells "never".
        float mWaterLevel = 0.0f;
    };

    /// The ray tracing renderer, as the game owns it.
    ///
    /// **A second renderer beside the first, not a replacement for it yet.** This slice is the spike
    /// `docs/rtx/plan.md` names at M2: get a traced image into the game window while the path is
    /// still simple enough to debug. The rasterizer still draws the world and the traced frame is
    /// composited over it — which is a wasted raster pass and is the point, because it separates
    /// "can Vulkan reach the screen" from "can the scene graph be mirrored every frame".
    class Tracer
    {
    public:
        /// Null where the renderer cannot start here, with `reason` saying why — no Vulkan loader,
        /// no device that qualifies, no OpenGL that can import what it makes.
        /// @param width, height the window's size in pixels, which is what comes out. What gets
        ///        traced is the upscaler's answer for it.
        static std::unique_ptr<Tracer> tryCreate(std::uint32_t width, std::uint32_t height,
            const std::filesystem::path& shaders, ::Rtx::Upscale upscale, std::string& reason);

        ~Tracer();

        Tracer(const Tracer&) = delete;
        Tracer& operator=(const Tracer&) = delete;

        /// Mirrors `scene` and traces one frame from `camera`.
        ///
        /// **Re-walked every frame, and most of it is kept.** The extractor's identity maps mean a
        /// mesh met again resolves to the one already uploaded, so what a second walk produces is a
        /// new list of placements over the same geometry — which is a top-level acceleration
        /// structure rebuild, the thing every renderer of this kind does per frame anyway.
        ///
        /// What it does not yet catch is geometry *arriving*: a mesh the walk meets for the first
        /// time needs a bottom-level structure and a texture upload, and this still asks for a full
        /// `setScene` when the instance count moves far enough to say a cell changed.
        ///
        /// Called after `updateTraversal` and before `renderingTraversals`, which is where the graph
        /// is settled and nothing has drawn yet.
        /// @param frame the viewer's frame number, which says which of a light source's two
        ///        buffers update has just finished writing.
        void trace(const osg::Node& scene, const osg::Camera& camera, const Lighting& lighting, std::size_t frame,
            Resource::ImageManager& images);

        /// Resizes to the window. The next trace exports a new allocation for the composite.
        void resize(std::uint32_t width, std::uint32_t height);

        /// The drawable that puts the last traced frame on the screen, for whoever adds it to a
        /// camera. Owned here; the camera holds a reference.
        Composite& getComposite() { return *mComposite; }

    private:
        Tracer(std::unique_ptr<::Rtx::Renderer> renderer, std::uint32_t width, std::uint32_t height);

        /// Hands the current frame's allocation to the composite. Once per resize.
        void share();

        /// Writes the traced frame to a numbered PNG, where `OPENMW_RTX_SHOT` asked for it.
        void keep();

        std::unique_ptr<::Rtx::Renderer> mRenderer;
        osg::ref_ptr<Composite> mComposite;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        /// Kept across frames, which is the whole of what makes a re-walk cheap: the identity maps
        /// inside the extractor are what resolve a mesh met again to the one already uploaded.
        ::Rtx::SceneDesc mScene;
        std::unique_ptr<RtxBridge::SceneExtractor> mExtractor;

        /// How many meshes the scene held after the last full build. A walk that finds more has met
        /// geometry that has no acceleration structure yet.
        std::size_t mBuilt = 0;

        /// A running average of what the trace costs, reported every `sReportEvery` frames.
        ///
        /// **The only instrument on this path.** The harness times a frame by tracing it thirty
        /// times and taking the best; a game cannot, so what it can say is what the last few hundred
        /// frames came to on average — which is the number that matters when the question is whether
        /// this is playable.
        double mSpentMs = 0.0;
        std::uint32_t mTimed = 0;

        std::size_t mFrame = 0;
        bool mComplained = false;
        bool mShared = false;

        /// Where `OPENMW_RTX_SHOT` says to write traced frames, and how many are left to write.
        ///
        /// **The game's answer to `openmw-rtxtool shot`.** Everything the tracer does inside the
        /// game — the lighting read off the renderer, the lights taken from the graph, the camera —
        /// is invisible to the harness, and checking it by opening a window and looking is the thing
        /// `CLAUDE.md` says not to do. A frame on disk is a frame anything can read.
        std::filesystem::path mKeepAt;
        std::uint32_t mKeepLeft = 0;

        /// Reused rather than allocated per frame, because this is a debug path and not an excuse.
        std::vector<std::uint8_t> mPixels;
    };
}

#endif
