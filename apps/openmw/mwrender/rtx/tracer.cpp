#include "tracer.hpp"

#include <osg/Camera>
#include <osg/Matrixf>
#include <osg/Node>

#include <components/debug/debuglog.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/texturebuilder.hpp>
#include <components/settings/values.hpp>

#include "composite.hpp"

namespace MWRender::Rtx
{
    namespace
    {
        /// Far enough to cross any cell. A primary ray that reaches this has left the world.
        constexpr float sFar = 200000.0f;

        /// A plain noon until the game's own weather is wired in.
        ///
        /// **A placeholder, and one worth naming rather than leaving as a black frame.** Everything
        /// the sky and the sun need is already something `RenderingManager` is handed —
        /// `setSunDirection`, `setSunColour`, `setAmbientColour`, `configureFog` — and connecting
        /// them is the next piece of M11, not this one.
        void litPlainly(::Rtx::Shaders::VisibilityConstants& camera)
        {
            camera.mSunDirection = osg::Vec3f(0.0f, 0.34f, -0.94f);
            camera.mSunIrradiance = osg::Vec3f(3.0f, 2.9f, 2.6f);
            camera.mSkyHorizon = osg::Vec3f(0.42f, 0.52f, 0.72f);
            camera.mSkyZenith = osg::Vec3f(0.18f, 0.30f, 0.62f);

            // **Enough that an interior is not black.** A room the outdoor sun cannot reach is lit
            // by its own `AMBI` and its own lamps, and this placeholder has neither — so without
            // this the first thing anyone sees of the ray tracer in the game is an unlit box, which
            // looks exactly like a broken handoff.
            camera.mAmbient = osg::Vec3f(0.12f, 0.12f, 0.14f);
        }

        /// Whether `makeCamera` can be built from this direction at all.
        ///
        /// **Asked here rather than caught there.** A camera with no roll is a contract `makeCamera`
        /// asserts by throwing, and it is right to: nothing can render from one. But the game hands
        /// over whatever its own camera is doing — including the frames of a cutscene where it looks
        /// straight down — and a frame the tracer cannot draw is a frame to skip, not a reason to
        /// take the game down with it.
        bool canLookAlong(const osg::Vec3f& forward)
        {
            if (!(forward.length2() > 0.0f))
                return false;

            osg::Vec3f along = forward;
            along.normalize();

            // The same test `makeCamera` makes: the cross product with the world's up vanishes.
            return (along ^ osg::Vec3f(0.0f, 0.0f, 1.0f)).length2() > 1e-6f;
        }
    }

    std::unique_ptr<Tracer> Tracer::tryCreate(
        std::uint32_t width, std::uint32_t height, const std::filesystem::path& shaders, std::string& reason)
    {
        ::Rtx::RendererOptions options;
        options.mShaderDirectory = shaders;
        options.mWidth = width;
        options.mHeight = height;

        // **No window and no upscaler.** The window belongs to OpenGL, which is why the frame is
        // exported rather than presented; and an upscaler would trace at a size the composite would
        // then blit from, which is a decision to take once the frame is on the screen at all.
        std::unique_ptr<::Rtx::Renderer> renderer = ::Rtx::createRenderer(options, reason);
        if (renderer == nullptr)
            return nullptr;

        return std::unique_ptr<Tracer>(new Tracer(std::move(renderer), width, height));
    }

    Tracer::Tracer(std::unique_ptr<::Rtx::Renderer> renderer, std::uint32_t width, std::uint32_t height)
        : mRenderer(std::move(renderer))
        , mComposite(new Composite)
        , mWidth(width)
        , mHeight(height)
    {
    }

    // Out of line because the members it destroys are only forward declared in the header.
    Tracer::~Tracer() = default;

    void Tracer::resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == mWidth && height == mHeight)
            return;

        mWidth = width;
        mHeight = height;
        mRenderer->resize(width, height);

        // The allocation moved, so what the composite imported points at nothing this renders.
        mShared = false;
    }

    void Tracer::share()
    {
        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        const ::Rtx::SharedFrame frame = mRenderer->shareFrame();
        mComposite->take(frame.mMemory, frame.mBytes, extents.mOutputWidth, extents.mOutputHeight);
        mShared = true;
    }

    void Tracer::trace(const osg::Node& scene, const osg::Camera& camera, Resource::ImageManager& images)
    {
        if (!mMirrored)
        {
            ::Rtx::SceneDesc described;
            RtxBridge::SceneExtractor extractor(described);
            const RtxBridge::ExtractionStats found
                = extractor.extract(const_cast<osg::Node&>(scene), osg::Matrixf::identity());

            if (described.getInstances().empty())
                return;

            Log(Debug::Info) << "Ray tracing mirrored " << found.mMeshesAdded << " meshes into " << found.mInstances
                             << " instances, and skipped " << found.mSkippedDeformed << " deformed";

            // The bridge decodes and describes; the backend uploads. Held only across the call —
            // `setScene` has finished with the descriptions when it returns.
            const RtxBridge::SceneTextures textures(described, images);
            mRenderer->setScene(described, textures.getDescriptions(), ::Rtx::SeaState{});
            mMirrored = true;
        }

        osg::Vec3f eye;
        osg::Vec3f centre;
        osg::Vec3f up;
        camera.getViewMatrixAsLookAt(eye, centre, up);

        if (!canLookAlong(centre - eye))
            return;

        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        ::Rtx::Shaders::VisibilityConstants constants = ::Rtx::makeCamera(
            eye, centre, Settings::camera().mFieldOfView, extents.mRenderWidth, extents.mRenderHeight, sFar);
        litPlainly(constants);

        // **Measured, not held at one.** A picture wants the exposure the frame asks for; holding
        // it is what a reference and a pixel test want, and the default is theirs. Without this an
        // interior lit by nothing but this placeholder's ambient reaches the screen at a few
        // hundredths and reads as black.
        mRenderer->renderFrame(constants, ::Rtx::FrameOptions{ .mExposure = std::nullopt });

        if (!mShared)
            share();
    }
}
