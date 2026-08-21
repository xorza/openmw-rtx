#include "tracer.hpp"

#include <cstdlib>
#include <format>

#include <osg/Camera>
#include <osg/Matrixf>
#include <osg/Node>

#include <components/debug/debuglog.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtxbridge/png.hpp>
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

        /// How often the trace's running average is reported. Five seconds at sixty frames.
        constexpr std::uint32_t sReportEvery = 300;

        /// How many frames `OPENMW_RTX_SHOT` writes before it stops. A cap rather than a count,
        /// because the alternative to a cap is filling a disk with a run somebody forgot about.
        constexpr std::uint32_t sKeepAtMost = 16;

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
        , mExtractor(std::make_unique<RtxBridge::SceneExtractor>(mScene))
        , mWidth(width)
        , mHeight(height)
    {
        if (const char* where = std::getenv("OPENMW_RTX_SHOT"); where != nullptr && *where != '\0')
        {
            mKeepAt = where;
            mKeepLeft = sKeepAtMost;
            Log(Debug::Info) << "Ray tracing will write its first " << mKeepLeft << " frames to " << mKeepAt
                             << "-0000.png and on";
        }
    }

    void Tracer::keep()
    {
        if (mKeepLeft == 0)
            return;

        --mKeepLeft;

        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        mRenderer->readPixels(mPixels);

        const std::filesystem::path file = mKeepAt.string() + std::format("-{:04}.png", sKeepAtMost - mKeepLeft - 1);

        try
        {
            RtxBridge::writePng(file, extents.mOutputWidth, extents.mOutputHeight, mPixels);
        }
        catch (const std::exception& failed)
        {
            mKeepLeft = 0;
            Log(Debug::Error) << "Ray tracing could not write " << file << ": " << failed.what();
        }
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

    void Tracer::trace(const osg::Node& scene, const osg::Camera& camera, const Lighting& lighting, std::size_t frame,
        Resource::ImageManager& images)
    {
        mFrame = frame;

        // **Every frame, and the placements are all that is thrown away.** `clearPlacement` keeps
        // the meshes, materials and texture paths — which is what the acceleration structures and
        // the texture array were built from — so a re-walk over an unchanged graph produces the same
        // geometry indices and a fresh list of where things are.
        mScene.clearPlacement();

        const RtxBridge::ExtractionStats found
            = mExtractor->extract(const_cast<osg::Node&>(scene), osg::Matrixf::identity(), mFrame);

        if (mScene.getInstances().empty())
            return;

        // Geometry the walk has not met before has no bottom-level structure and no uploaded
        // texture, so the whole scene is rebuilt rather than replaced. **Which is a cell change and
        // a load, not a frame** — a door opening moves instances the walk already knows.
        const bool arrived = mScene.getMeshes().size() != mBuilt;
        if (arrived)
        {
            Log(Debug::Info) << "Ray tracing built " << mScene.getMeshes().size() << " meshes into " << found.mInstances
                             << " instances with " << found.mLights << " lights, and skipped " << found.mSkippedDeformed
                             << " deformed";

            // The bridge decodes and describes; the backend uploads. Held only across the call —
            // `setScene` has finished with the descriptions when it returns.
            const RtxBridge::SceneTextures textures(mScene, images);
            if (const std::uint32_t missing = textures.getUnreadable(); missing > 0)
                Log(Debug::Warning) << "Ray tracing could not read " << missing << " of "
                                    << textures.getDescriptions().size()
                                    << " textures and drew them grey — a live graph holds textures that were "
                                       "never files";

            mRenderer->setScene(mScene, textures.getDescriptions(), ::Rtx::SeaState{});
            mBuilt = mScene.getMeshes().size();
        }
        else
        {
            mRenderer->placeScene(mScene, ::Rtx::SeaState{});
        }

        osg::Vec3f eye;
        osg::Vec3f centre;
        osg::Vec3f up;
        camera.getViewMatrixAsLookAt(eye, centre, up);

        if (!canLookAlong(centre - eye))
        {
            // Once, because a frame the tracer cannot draw is usually a cutscene and occasionally a
            // camera nobody has filled in — and the two look identical from here until it is said
            // out loud how often it happens.
            if (!mComplained)
            {
                mComplained = true;
                Log(Debug::Warning) << "Ray tracing skipped a frame: the camera at " << eye.x() << ", " << eye.y()
                                    << ", " << eye.z() << " looks along " << (centre - eye).x() << ", "
                                    << (centre - eye).y() << ", " << (centre - eye).z();
            }
            return;
        }

        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        ::Rtx::Shaders::VisibilityConstants constants = ::Rtx::makeCamera(
            eye, centre, Settings::camera().mFieldOfView, extents.mRenderWidth, extents.mRenderHeight, sFar);
        constants.mSunDirection = lighting.mSunDirection;
        constants.mSunIrradiance = lighting.mSunIrradiance;
        constants.mAmbient = lighting.mAmbient;
        constants.mWaterLevel = lighting.mWaterLevel;

        // The horizon is the fog, and the zenith with it until the sky's own colour is reached.
        // **It goes to `SkyManager` and never through `RenderingManager`**, so there is nothing
        // settled to read it off — which is a wire to run, not a number to invent.
        constants.mSkyHorizon = lighting.mFog;
        constants.mSkyZenith = lighting.mFog;

        constants.mFogColour = lighting.mFog;
        constants.mFogExtinction = lighting.mFogExtinction;

        // **Measured, not held at one.** A picture wants the exposure the frame asks for; holding
        // it is what a reference and a pixel test want, and the default is theirs. Without this an
        // interior lit by nothing but this placeholder's ambient reaches the screen at a few
        // hundredths and reads as black.
        const ::Rtx::FrameResult result
            = mRenderer->renderFrame(constants, ::Rtx::FrameOptions{ .mExposure = std::nullopt });

        mSpentMs += result.mTraceMs;
        if (++mTimed == sReportEvery)
        {
            Log(Debug::Info) << "Ray tracing: " << mSpentMs / mTimed << " ms a frame over the last " << mTimed
                             << ", tracing " << mScene.getInstances().size() << " instances at " << extents.mRenderWidth
                             << "x" << extents.mRenderHeight;
            mSpentMs = 0.0;
            mTimed = 0;
        }

        if (!mShared)
            share();

        // **After the share and not before**, because reading the frame back moves it out of the
        // layout the composite blits from and the next frame's trace is what puts it back.
        keep();
    }
}
