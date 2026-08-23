#ifndef GAME_RENDER_RENDERER_H
#define GAME_RENDER_RENDERER_H

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string_view>

#include <osg/Timer>

#include <components/sdlutil/vsyncmode.hpp>
#include <components/vfs/pathutil.hpp>

struct SDL_Window;

namespace osg
{
    class Group;
    class Image;
}

namespace osgUtil
{
    class IncrementalCompileOperation;
}

namespace MyGUIPlatform
{
    class Platform;
}

namespace Resource
{
    class ImageManager;
}

namespace SceneUtil
{
    class WorkQueue;
}

namespace Shader
{
    class ShaderManager;
}

namespace VFS
{
    class Manager;
}

namespace MWRender
{
    class PostProcessor;
    class RenderingManager;
    struct SceneFrame;
    class Stage;

    /// What this renderer can do, so nothing above it has to assume.
    ///
    /// **Capabilities rather than null returns.** A renderer that is not the rasterizer will not
    /// have some of what the rasterizer has, and the difference has to be answerable *before* a
    /// settings page offers a slider for it or a Lua script calls into it. A ray tracer answering
    /// "no shadow maps" is not reporting a gap: it has shadows and no maps, and that is the correct
    /// answer.
    ///
    /// One field so far, because a gate whose false branch no renderer takes is code nothing can
    /// test. The rest of `docs/rtx/renderers.md` §8 arrives with the renderer that answers false.
    struct Capabilities
    {
        /// How many textures one shader may sample. `Shader::ShaderManager` reserves its global
        /// units out of this, and content that wants one more than there are has to be told rather
        /// than find out at link time.
        int mTextureUnits = 0;

        /// Whether this renderer will ever have a shader chain over the frame.
        ///
        /// **Asked only by what runs before there is one.** `getPostProcessor()` is the answer
        /// everywhere else, and null is a reply the Lua bindings, the settings page and the HUD all
        /// already understand — but the GUI is built before the world is, so whether to make the
        /// post-processing HUD at all cannot be answered by asking for a chain that does not exist
        /// yet. A renderer with none is not missing one: light and water reach its picture by a
        /// different route entirely.
        bool mPostProcessing = false;
    };

    /// What every renderer needs to exist, whatever it draws with.
    struct RendererSpec
    {
        /// The frame, the eye and the input queue. Made before any renderer and outliving it: the
        /// renderer adopts these objects rather than making its own, which is what lets the game
        /// hold the camera and the frame stamp without knowing what draws.
        Stage& mStage;

        /// For work a frame must not wait on — writing a screenshot to disk, so far.
        SceneUtil::WorkQueue& mWorkQueue;

        /// Where the window icon is read from.
        std::filesystem::path mResourceDir;

        std::filesystem::path mScreenshotPath;
    };

    /// One image of the world on the screen, and the window it goes in.
    ///
    /// **Nothing below this line is abstracted.** Contexts, swapchains, command buffers,
    /// framebuffers, render bins, descriptor sets and acceleration structures belong to a renderer
    /// outright — an interface over those would be a mini-GL that Vulkan does not fit, which is the
    /// argument `docs/rtx/backends.md` §3 makes one level further down for the same reason. What is
    /// here is what the game asks for, and none of it is called more than a few times a frame.
    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        Renderer(const Renderer&) = delete;
        Renderer& operator=(const Renderer&) = delete;

        virtual const Capabilities& getCapabilities() const = 0;

        /// The window the renderer made. Input, the GUI's scale and the gamma ramp are SDL's
        /// business and read it; what is bound to it is not theirs to know.
        virtual SDL_Window* getWindow() const = 0;

        /// The world exists; build whatever goes between it and the screen.
        ///
        /// **A second phase because it cannot be a first one.** The renderer is made before there is
        /// a world — the window has to exist before anything can be loaded into it — and what the
        /// rasterizer puts in front of the world needs the world to talk to. So the frame graph is
        /// built here, along with whatever else a renderer wants above the scene, and this is also
        /// where the stage is told what is topmost.
        ///
        /// Called once, from `RenderingManager`'s constructor.
        virtual void attachWorld(RenderingManager& world, osg::Group& worldRoot) = 0;

        /// Whatever the renderer wants culled and drawn. Not always the node the world was built
        /// under: the rasterizer wraps it in its post-processing group and hands back the wrapper.
        virtual void setSceneRoot(osg::Group& root) = 0;

        /// The shader chain over the frame, or null where this renderer has none.
        ///
        /// **Owned here and not by the world.** It is a renderer's answer to "what happens between
        /// the scene and the screen", which is the whole of what a renderer is for; a world that
        /// held one would have to know whether the renderer it was given wanted it. Eleven Lua
        /// bindings, the HUD and the settings page read this and already treat null as "no chain".
        virtual PostProcessor* getPostProcessor() { return nullptr; }

        /// Stamps the next frame. Simulation time stops when the game is paused; reference time
        /// does not.
        virtual void advance(double simulationTime) = 0;

        virtual void eventTraversal() = 0;
        virtual void updateTraversal() = 0;

        /// The world, and the GUI over it. Once per frame, from the main loop.
        virtual void renderFrame(const SceneFrame& frame) = 0;

        /// The GUI, with no world behind it.
        ///
        /// **The four places that get a frame onto the screen from inside another one** — the
        /// loading screen, a modal message box, a video and the screenshot — and none of them has a
        /// world to describe. A renderer that culls cannot tell the two apart and answers both the
        /// same way; one that mirrors the graph and traces it very much can.
        virtual void renderGui() = 0;

        /// Whether the window has been closed.
        virtual bool done() const = 0;

        /// The frame without the GUI, into an image. The screenshot console command and the save
        /// thumbnails; blocks until the frame it asked for has been drawn.
        virtual void capture(osg::Image& image, int width, int height) = 0;

        /// The screenshot key, which writes a file rather than handing back an image.
        virtual void saveScreenshot() = 0;

        /// Between these two nothing is reading the scene graph, so it can be mutated. A renderer
        /// that draws on the calling thread answers both with nothing.
        virtual void suspendDraw() = 0;
        virtual void resumeDraw() = 0;

        /// Compiles arriving resources over several frames instead of stalling on first use. Null
        /// where `OPENMW_DONT_PRECOMPILE` asked for none, which is why this one is a pointer.
        virtual osgUtil::IncrementalCompileOperation* getCompileOperation() const = 0;
        virtual void setCompileOperation(osgUtil::IncrementalCompileOperation* operation) = 0;

        virtual void setVSync(SDLUtil::VSyncMode mode) = 0;

        /// Recompiles whatever shader source has been edited since the last call. Costs a directory
        /// scan and nothing else when the feature is off, which is what makes it callable per frame.
        virtual void reloadChangedShaders(Shader::ShaderManager& shaders) = 0;

        /// The origin the per-frame profiler measures from, so its spans land on the same axis as
        /// the counters the renderer writes beside them.
        virtual osg::Timer_t getStartTick() const = 0;

        /// This renderer's own instrumentation — the overlay its debug keys toggle, and the
        /// per-frame dump `OPENMW_OSG_STATS_FILE` asks for. What it counts is its own business, so
        /// what it draws and what it writes are too.
        virtual void installStatsOverlay(const VFS::Manager& vfs, bool toFile) = 0;
        virtual void reportStats(unsigned frameNumber, std::ostream& stream) const = 0;

        /// MyGUI's backend. `MyGUI::RenderManager` is MyGUI's own interface, so a second one of
        /// these is a second implementation of an existing interface rather than a new abstraction.
        virtual std::unique_ptr<MyGUIPlatform::Platform> createGuiPlatform(osg::Group& guiRoot,
            Resource::ImageManager& images, const VFS::Manager& vfs, float scalingFactor,
            VFS::Path::NormalizedView resourcePath, const std::filesystem::path& logPath)
            = 0;

    protected:
        Renderer() = default;
    };

    /// The one place the choice is made. Throws naming the name where there is no such renderer,
    /// because a fallback would answer "why does it look like that" with silence.
    std::unique_ptr<Renderer> createRenderer(std::string_view name, const RendererSpec& spec);
}

#endif
