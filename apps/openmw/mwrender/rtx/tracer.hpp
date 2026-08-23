#ifndef GAME_RENDER_RTX_TRACER_H
#define GAME_RENDER_RTX_TRACER_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <osg/ref_ptr>

#include <components/rtx/scenedesc.hpp>
#include <components/rtx/upscale.hpp>
#include <components/rtxbridge/sceneuploader.hpp>

#include "../sceneframe.hpp"

#include "bench.hpp"

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
        /// Geometry *arriving* is appended rather than rebuilt: a mesh the walk meets for the first
        /// time gets a bottom-level structure and its textures go onto the end of the array, and only
        /// a sweep that renumbered the tables costs a full `setScene`.
        ///
        /// Called after `updateTraversal` and before `renderingTraversals`, which is where the graph
        /// is settled and nothing has drawn yet.
        /// @param when the viewer's frame stamp. Its frame number says which of a light source's
        ///        two buffers update has just finished writing; its simulation time is the world's
        ///        clock, which drives the sea and everything the graph animates — and which stops
        ///        when the game is paused, as both should.
        void trace(const SceneFrame& frame);

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

        /// Which of place, extend and rebuild a frame is, and what a rebuild has to describe.
        RtxBridge::SceneUploader mUploader;

        /// A running average of what the trace costs, reported every `sReportEvery` frames.
        ///
        /// **The only instrument on this path.** The harness times a frame by tracing it thirty
        /// times and taking the best; a game cannot, so what it can say is what the last few hundred
        /// frames came to on average — which is the number that matters when the question is whether
        /// this is playable.
        double mSpentMs = 0.0;
        std::uint32_t mTimed = 0;

        /// Times a run of frames when asked to, and is not compiled at all when it cannot be.
        Bench mBench;

        /// When the last frame was handed over, so what `Bench` measures is the whole frame and not
        /// this renderer's slice of it.
        std::chrono::steady_clock::time_point mEntered;
        bool mEnteredOnce = false;

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
