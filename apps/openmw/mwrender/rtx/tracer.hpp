#ifndef GAME_RENDER_RTX_TRACER_H
#define GAME_RENDER_RTX_TRACER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include <osg/ref_ptr>

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
        static std::unique_ptr<Tracer> tryCreate(
            std::uint32_t width, std::uint32_t height, const std::filesystem::path& shaders, std::string& reason);

        ~Tracer();

        Tracer(const Tracer&) = delete;
        Tracer& operator=(const Tracer&) = delete;

        /// Mirrors `scene` the first time it is asked and traces one frame from `camera`.
        ///
        /// **Mirrored once and not per frame**, which is this slice's limitation and not a design:
        /// `RtxBridge::SceneExtractor` rebuilds a whole `SceneDesc` from a graph, and the game's
        /// graph changes every frame. Change tracking is the next piece and it is the large one.
        ///
        /// Called after `updateTraversal` and before `renderingTraversals`, which is where the graph
        /// is settled and nothing has drawn yet.
        void trace(const osg::Node& scene, const osg::Camera& camera, Resource::ImageManager& images);

        /// Resizes to the window. The next trace exports a new allocation for the composite.
        void resize(std::uint32_t width, std::uint32_t height);

        /// The drawable that puts the last traced frame on the screen, for whoever adds it to a
        /// camera. Owned here; the camera holds a reference.
        Composite& getComposite() { return *mComposite; }

    private:
        Tracer(std::unique_ptr<::Rtx::Renderer> renderer, std::uint32_t width, std::uint32_t height);

        /// Hands the current frame's allocation to the composite. Once per resize.
        void share();

        std::unique_ptr<::Rtx::Renderer> mRenderer;
        osg::ref_ptr<Composite> mComposite;

        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;

        bool mMirrored = false;
        bool mShared = false;
    };
}

#endif
