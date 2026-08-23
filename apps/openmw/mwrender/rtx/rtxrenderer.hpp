#ifndef GAME_RENDER_RTX_RTXRENDERER_H
#define GAME_RENDER_RTX_RTXRENDERER_H

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include <osg/ref_ptr>

#include <components/myguiplatform/picture.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneuploader.hpp>

#include "../renderer.hpp"

#include "bench.hpp"

namespace Resource
{
    class ResourceSystem;
}

namespace osg
{
    class Camera;
    class FrameStamp;
    class Stats;
}

namespace osgGA
{
    class EventQueue;
}

namespace osgUtil
{
    class UpdateVisitor;
}

namespace Rtx
{
    class Renderer;
}

namespace RtxBridge
{
    struct ExtractionStats;
    class SceneExtractor;
}

namespace MWRender::Rtx
{
    /// The picture as rays find it: a window, a mirror of the scene graph, and a trace.
    ///
    /// **It names no graphics API, and there is no `metal/` beside it.** Which API traces is settled
    /// a layer down — `Rtx::createRenderer` picks `components/rtxvulkan` or `components/rtxmetal` by
    /// what the build is for — so everything here is written once for both: the mirror, the frame,
    /// the extents, the capabilities. A second copy of this directory is exactly the duplication
    /// `.notes/rtx/backends.md` exists to prevent. `mwrender/gl/` is asymmetric with it for a reason:
    /// that renderer *is* an API, down to its sky and its water.
    ///
    /// **No OpenGL is initialised anywhere under this.** No GL context, no `osgViewer` graphics
    /// window, no interop and no rasterized frame underneath — the window is an SDL surface the
    /// backend builds its own surface on, and what reaches the screen is what the trace wrote.
    ///
    /// **It drives the frame itself.** `advance`, `eventTraversal` and `updateTraversal` are
    /// `osgViewer::Viewer`'s, and each is scene-graph work with a graphics context bolted to the
    /// side; what is here is the first half of each and nothing else. There is no cull: rays go
    /// everywhere, so a frustum has nothing to say about what must be reachable — which is also
    /// why the frame is not one late the way `.notes/rtx/plan.md` §12 describes. The mirror runs
    /// after the update traversal and the present runs after the mirror, all inside one frame.
    class TracedView;

    class RtxRenderer final : public MWRender::Renderer
    {
    public:
        /// Throws `std::runtime_error` naming what stopped it — no loader for the backend's API,
        /// no device that qualifies, an upscale mode this build cannot provide. Never falls back: a renderer that
        /// quietly became a different one answers "why does it look like that" with silence.
        explicit RtxRenderer(const RendererSpec& spec);
        ~RtxRenderer() override;

        int getMaxTextureUnits() const override { return mMaxTextureUnits; }
        SDL_Window* getWindow() const override { return mWindow; }

        void attachWorld(RenderingManager& world, osg::Group& worldRoot) override;
        void setSceneRoot(osg::Group& root) override;

        void advance(double simulationTime) override;
        void eventTraversal() override;
        void updateTraversal() override;

        void renderFrame(const SceneFrame& frame) override;

        /// **A trace into a texture the GUI already draws from**, at the size asked for and from
        /// the viewpoint handed over: the inventory doll, the race preview, a map tile. A picture of
        /// the world traces against the scene this renderer already holds; a picture of a subject
        /// that stands in no cell is mirrored into a scene of its own.
        std::unique_ptr<OffscreenView> createOffscreenView(const OffscreenViewSpec& spec) override;

        /// **Flat black, and the loading screen puts it up as the backdrop.** What this owes is the
        /// frame just presented, blitted into a GUI texture; what it gives is one black texel, said
        /// on purpose rather than left to whatever the texture happened to hold.
        MyGUI::ITexture& freezeFrame() override;

        void renderGui() override;

        bool done() const override { return false; }

        void capture(osg::Image& image, int width, int height) override;
        void saveScreenshot() override;

        /// Nothing draws on another thread, so there is nothing to hold still.
        void suspendDraw() override {}
        void resumeDraw() override {}

        /// No OpenGL objects exist to compile over several frames, and `LoadingScreen` already
        /// reads null as "there is no such thing here".
        osgUtil::IncrementalCompileOperation* getCompileOperation() const override { return nullptr; }
        void setCompileOperation(osgUtil::IncrementalCompileOperation* operation) override {}

        /// The swapchain picks its own present mode and does not offer to change it while it is
        /// up. Answering this would mean rebuilding it — a stall — for a setting nothing on this
        /// path has asked for yet.
        void setVSync(SDLUtil::VSyncMode mode) override {}

        /// GLSL is the rasterizer's language. What this renderer draws with is compiled SPIR-V, and
        /// swapping it under a running frame is not a thing it offers.
        void reloadChangedShaders(Shader::ShaderManager& shaders) override {}

        std::unique_ptr<MyGUIPlatform::Platform> createGuiPlatform(osg::Group& guiRoot, Resource::ImageManager& images,
            Shader::ShaderManager& shaders, const VFS::Manager& vfs, float scalingFactor,
            VFS::Path::NormalizedView resourcePath, const std::filesystem::path& logPath) override;

        osg::Timer_t getStartTick() const override { return mStartTick; }

        /// The OSG stats overlay is the rasterizer's instrumentation and the rasterizer draws it.
        /// What this renderer has instead is its own frame times and `OPENMW_RTX_BENCH`.
        void installStatsOverlay(const VFS::Manager& vfs, bool toFile) override {}
        void reportStats(unsigned frameNumber, std::ostream& stream) const override {}

        /*internal:*/

        /// Whether the world has reached the backend yet, so a picture traced against it would be a
        /// picture of something.
        bool hasScene() const { return mHasScene; }

        /// Draws `view` on the next frame that has a world in it.
        ///
        /// **A cell asks for its map tile as it loads**, which is the frame before the one that
        /// first mirrors it. Without this the tile the player starts on stays blank until a
        /// neighbour arriving makes the local map ask for it again.
        void deferRedraw(TracedView& view);

        /// Takes a view off that list, because it is going away.
        void forgetView(TracedView& view);

        /// Where a picture of its own subject gets its textures from. Null before there is a world.
        Resource::ResourceSystem* getResources() const { return mResources; }

        /// Runs one update traversal over a subtree that is not in the graph.
        ///
        /// **Nothing else will.** The rasterizer hangs an offscreen view's camera off the scene
        /// graph, so the viewer's own update traversal reaches the subtree under it and poses the
        /// character; there is no such graph here, and a doll that is never updated is a doll in the
        /// bind pose with a camera that never found its head.
        /// @return the traversal number it posed at, which is the frame a double-buffered pose
        ///         was written into and so the one an intersection test has to read.
        unsigned int updateSubtree(osg::Node& node);

    private:
        /// Makes the SDL window the backend builds its surface on. No GL attribute is set and no GL
        /// flag is passed, which is what `SDL_GL_GetCurrentContext() == nullptr` then proves.
        void createWindow(const std::filesystem::path& resourceDir);

        /// Resizes the trace to the window where the window has changed under it.
        void fitToWindow();

        /// Traces the world the walk has just mirrored, from the eye the frame arrived with.
        ///
        /// **Its refusals are not the frame's.** A world with nothing in it and a camera with no
        /// roll are both reasons not to trace and neither is a reason not to present, so they end
        /// here rather than in `renderFrame` — see the comment on the call.
        ///
        /// @return whether anything was written into the target.
        bool traceWorld(const SceneFrame& frame, const RtxBridge::ExtractionStats& found);

        /// Writes the traced frame to a numbered PNG, where `OPENMW_RTX_SHOT` asked for it.
        void keep();

        /// Hands MyGUI's triangles to the renderer, where there is a GUI up at all.
        void drawGui();

        /// Draws whatever asked before there was a world to draw it against.
        void drawDeferredViews();

        Stage& mStage;
        int mMaxTextureUnits = 0;

        /// Whether the world has been handed to the backend at least once.
        bool mHasScene = false;

        /// The world's, for a picture that has to resolve textures of its own. Null until
        /// `attachWorld`.
        Resource::ResourceSystem* mResources = nullptr;

        /// Pictures that asked to be drawn before it had. Raw pointers because the caller owns
        /// every view; `forgetView` is what keeps that sound.
        std::vector<TracedView*> mDeferred;

        /// The list a flush walks, swapped out of `mDeferred` so a redraw cannot grow what is being
        /// iterated. Kept rather than made, because this sits on the frame path.
        std::vector<TracedView*> mDrawing;

        MyGUIPlatform::Picture mFrozenFrame{ "frozen frame" };

        SDL_Window* mWindow = nullptr;

        /// What the stage was handed. Made here because there is no viewer to make them, and held
        /// because the frame is driven from them.
        osg::ref_ptr<osg::Camera> mCamera;
        osg::ref_ptr<osg::FrameStamp> mFrameStamp;
        osg::ref_ptr<osgGA::EventQueue> mEvents;
        osg::ref_ptr<osgUtil::UpdateVisitor> mUpdateVisitor;
        osg::ref_ptr<osg::Stats> mStats;
        osg::ref_ptr<osg::Group> mSceneRoot;

        /// Where `advance` measures reference time from, and the origin the profiler's spans are
        /// stamped against.
        osg::Timer_t mStartTick = 0;

        std::unique_ptr<::Rtx::Renderer> mRenderer;

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

        /// The frame number the walk and the trace are both stamped with, so what the upscaler
        /// jitters and what the sampler walks are the same sequence the world is counting.
        std::size_t mFrame = 0;
        bool mComplained = false;

        /// Where `OPENMW_RTX_SHOT` says to write traced frames, and how many are left to write.
        std::filesystem::path mKeepAt;
        std::uint32_t mKeepLeft = 0;

        /// Reused rather than allocated per frame, because this is a debug path and not an excuse.
        std::vector<std::uint8_t> mPixels;
    };
}

#endif
