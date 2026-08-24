#include "rtxrenderer.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <numbers>
#include <stdexcept>

#include <SDL.h>

#include <osg/Camera>
#include <osg/FrameStamp>
#include <osg/Image>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Stats>
#include <osg/Timer>

#include <osgDB/ReaderWriter>
#include <osgDB/Registry>

#include <osgGA/EventQueue>

#include <osgUtil/UpdateVisitor>

#include <MyGUI_ITexture.h>
#include <MyGUI_RenderManager.h>

#include <components/debug/debuglog.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/myguirtx/rendermanager.hpp>
#include <components/rtx/camera.hpp>
#include <components/rtx/renderer.hpp>
#include <components/rtx/scenedesc.hpp>
#include <components/rtx/shaders/scene.h>
#include <components/rtx/upscale.hpp>
#include <components/rtxbridge/frameimage.hpp>
#include <components/rtxbridge/lightbuilder.hpp>
#include <components/rtxbridge/png.hpp>
#include <components/rtxbridge/sceneextractor.hpp>
#include <components/rtxbridge/sceneuploader.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sdlutil/imagetosurface.hpp>
#include <components/settings/values.hpp>

#include "../offscreenview.hpp"
#include "../sceneframe.hpp"
#include "../stage.hpp"
#include "../windowsetup.hpp"
#include <components/resource/resourcesystem.hpp>

#include "../renderingmanager.hpp"
#include "../screenshotwriter.hpp"
#include "../vismask.hpp"

#include "tracedview.hpp"

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

        /// Whether `makeCameraAlong` can be built from this direction at all.
        ///
        /// **Asked here rather than caught there.** A camera with no roll is a contract
        /// `makeCameraAlong` asserts by throwing, and it is right to: nothing can render from one.
        /// But the game hands over whatever its own camera is doing — including the frames of a
        /// cutscene where it looks straight down — and a frame the tracer cannot draw is a frame to
        /// skip, not a reason to take the game down with it.
        bool canLookAlong(const osg::Vec3f& forward)
        {
            if (!(forward.length2() > 0.0f))
                return false;

            osg::Vec3f along = forward;
            along.normalize();

            // The same test `makeCameraAlong` makes: the cross product with the world's up vanishes.
            return (along ^ osg::Vec3f(0.0f, 0.0f, 1.0f)).length2() > 1e-6f;
        }
    }

    RtxRenderer::RtxRenderer(const RendererSpec& spec)
        : mStage(spec.mStage)
        , mCamera(new osg::Camera)
        , mFrameStamp(new osg::FrameStamp)
        , mEvents(new osgGA::EventQueue)
        , mUpdateVisitor(new osgUtil::UpdateVisitor)
        , mStats(new osg::Stats("Viewer"))
        , mStartTick(osg::Timer::instance()->tick())
        , mExtractor(std::make_unique<RtxBridge::SceneExtractor>(mScene))
    {
        // **What the shader visitor is told a GPU offers.** It runs on every model OpenMW loads
        // and needs a number to fit texture slots into; without a GL context there is nothing to
        // ask. The value only decides how many slots it is willing to use — the roles it labels
        // them with, which is all this renderer reads, are the same either way. The harness has
        // assumed the same number since M0.
        mMaxTextureUnits = 32;

        mFrameStamp->setFrameNumber(0);
        mFrameStamp->setReferenceTime(0.0);
        mFrameStamp->setSimulationTime(0.0);
        mUpdateVisitor->setFrameStamp(mFrameStamp);

        mScreenshotWriter = makeScreenshotWriter(spec.mWorkQueue, spec.mScreenshotPath);

        createWindow(spec.mResourceDir);

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(mWindow, &width, &height);
        mWidth = static_cast<std::uint32_t>(std::max(width, 1));
        mHeight = static_cast<std::uint32_t>(std::max(height, 1));

        mStage.adopt(*mCamera, *mFrameStamp, *mEvents, *mUpdateVisitor, *mStats);

        const std::string wanted = Settings::rtx().mUpscale;

        // **Refused rather than defaulted**, for the reason `Rtx::upscaleNamed` gives: a typo that
        // quietly renders at another mode is a measurement of the wrong thing.
        const std::optional<::Rtx::Upscale> upscale = ::Rtx::upscaleNamed(wanted);
        if (!upscale.has_value())
            throw std::runtime_error('"' + wanted + "\" is not one of off, performance, balanced, quality or dlaa");

        ::Rtx::RendererOptions options;
        options.mShaderDirectory = spec.mResourceDir / "rtx" / "shaders";
        options.mWidth = mWidth;
        options.mHeight = mHeight;
        options.mUpscale = *upscale;
        options.mWindow = mWindow;
        options.mValidation.mEnabled = Settings::rtx().mValidation;

        std::string reason;
        mRenderer = ::Rtx::createRenderer(options, reason);
        if (mRenderer == nullptr)
            throw std::runtime_error("no ray tracing renderer: " + reason);

        Log(Debug::Info) << "Ray tracing on " << mRenderer->describeDevice();

        // **The renderer's own extent and not SDL's.** A windowed backend sizes itself to the
        // surface, and on a scaled or tiling compositor that is not what the window was asked for.
        // Everything above reads the viewport, so it has to be told what was actually built.
        fitToWindow();

        // **The sky is not mirrored.** It is the one subtree the engine rebuilds every frame —
        // state sets and all — so walking it churns the identity maps, and a sweep that drops four
        // materials a frame bumps the revision and makes every frame a full rebuild. Nothing is
        // lost by leaving it out: a ray that reaches the sky has missed everything, and what it
        // gets then is this renderer's own sky rather than the dome the rasterizer draws.
        //
        // **And `Mask_SimpleWater` with them, which is a duplicate rather than a subtree to skip.**
        // `MWRender::Water` hangs two coplanar quads under one node — the world's water under
        // `Mask_Water`, and a deep copy of it under `Mask_SimpleWater` that exists for the local
        // map — and the rasterizer picks between them with the drawing camera's traversal mask.
        // A mirror that walks both places the sea twice, at the same height, as two meshes.
        mExtractor->setTraversalMask(~static_cast<osg::Node::NodeMask>(Mask_Sky | Mask_Sun | Mask_SimpleWater));

        // What is left of the two is the world's own water, and it is the sea.
        mExtractor->setWaterMask(Mask_Water);

        // **The negative test, and it is the whole claim of this path in one line.** Nothing above
        // here may have made a GL context: not the window, not a realize operation, not an
        // `osgViewer` that slipped back in. A context that exists is one something is paying for.
        if (SDL_GL_GetCurrentContext() != nullptr)
            throw std::runtime_error("something initialised OpenGL under the ray tracing renderer");

        if (const char* where = std::getenv("OPENMW_RTX_SHOT"); where != nullptr && *where != '\0')
        {
            mKeepAt = where;
            mKeepLeft = sKeepAtMost;
            Log(Debug::Info) << "Ray tracing will write its first " << mKeepLeft << " frames to " << mKeepAt
                             << "-0000.png and on";
        }
    }

    // Out of line because the members it destroys are only forward declared in the header.
    RtxRenderer::~RtxRenderer()
    {
        // Before the renderer, because a write still on the queue holds an image of a frame this
        // owns the memory for.
        if (mScreenshotWriter != nullptr)
            mScreenshotWriter->stop();

        mRenderer.reset();

        if (mWindow != nullptr)
            SDL_DestroyWindow(mWindow);
    }

    void RtxRenderer::createWindow(const std::filesystem::path& resourceDir)
    {
        // **The backend's own flag, and no `SDL_GL_SetAttribute` anywhere near it.** Which one it
        // is — Vulkan here, Metal on Apple silicon — is the one thing about the API this file would
        // otherwise have had to know, and `Rtx::surfaceWindowFlag` is where that is settled. No GL
        // context is ever made, which is the point of the whole path.
        const WindowPlacement placement = describeWindow(::Rtx::surfaceWindowFlag());

        mWindow = SDL_CreateWindow(
            "OpenMW", placement.mX, placement.mY, placement.mWidth, placement.mHeight, placement.mFlags);
        if (mWindow == nullptr)
            throw std::runtime_error(std::string("failed to create SDL window: ") + SDL_GetError());

        MWRender::setWindowIcon(*mWindow, resourceDir);
    }

    void RtxRenderer::attachWorld(RenderingManager& world, osg::Group& worldRoot)
    {
        // Only for the pictures inside the interface: a doll resolves its own textures, and this is
        // where they come from. Nothing about the frame needs it — the mirror is handed an image
        // manager by whoever drives it.
        mResources = world.getResourceSystem();

        // Nothing goes between the world and the screen: what the trace writes is the picture.
        setSceneRoot(worldRoot);
    }

    unsigned int RtxRenderer::updateSubtree(osg::Node& node)
    {
        const unsigned int at = static_cast<unsigned int>(mFrameStamp->getFrameNumber());

        mUpdateVisitor->reset();
        mUpdateVisitor->setFrameStamp(mFrameStamp);
        mUpdateVisitor->setTraversalNumber(at);
        node.accept(*mUpdateVisitor);

        return at;
    }

    void RtxRenderer::setSceneRoot(osg::Group& root)
    {
        mSceneRoot = &root;
        mStage.setSceneRoot(root);
    }

    void RtxRenderer::advance(double simulationTime)
    {
        const double previousReferenceTime = mFrameStamp->getReferenceTime();
        const unsigned int previousFrame = mFrameStamp->getFrameNumber();

        mFrameStamp->setFrameNumber(previousFrame + 1);
        mFrameStamp->setReferenceTime(osg::Timer::instance()->delta_s(mStartTick, osg::Timer::instance()->tick()));
        mFrameStamp->setSimulationTime(simulationTime);

        // The same two the viewer writes, because the profiler's own spans are reported against
        // them and a frame with neither reads as a frame that took no time.
        if (mStats->collectStats("frame_rate"))
        {
            const double spent = mFrameStamp->getReferenceTime() - previousReferenceTime;
            mStats->setAttribute(previousFrame, "Frame duration", spent);
            mStats->setAttribute(previousFrame, "Frame rate", spent > 0.0 ? 1.0 / spent : 0.0);
            mStats->setAttribute(mFrameStamp->getFrameNumber(), "Reference time", mFrameStamp->getReferenceTime());
        }
    }

    void RtxRenderer::eventTraversal()
    {
        // **Drained and dropped.** What SDL puts in here is the function keys, which upstream reads
        // with `osgViewer` handlers this renderer does not have; everything the game itself acts on
        // came through `SDLUtil::InputWrapper` and MyGUI long before this. Leaving the queue to grow
        // is the only way to get this wrong.
        osgGA::EventQueue::Events events;
        mEvents->takeEvents(events);
    }

    void RtxRenderer::updateTraversal()
    {
        // **Before the early return, because a main menu has no scene root.** MyGUI's widget
        // animation, its key repeat and its tooltip timers all hang off this one call, and the other
        // backend gets it from an update callback on a node that is always in the graph.
        if (MyGUIRtx::RenderManager* gui = MyGUIRtx::RenderManager::getInstancePtr())
            gui->update();

        if (mSceneRoot == nullptr)
            return;

        mUpdateVisitor->reset();
        mUpdateVisitor->setFrameStamp(mFrameStamp);
        mUpdateVisitor->setTraversalNumber(mFrameStamp->getFrameNumber());
        mSceneRoot->accept(*mUpdateVisitor);

        // **And the eye, which is not in the graph.** `MWRender::Camera` puts where the player is
        // looking onto the master camera from an update callback, exactly as the viewer's own update
        // traversal reaches it. Without this the view matrix is whatever it was made with, and every
        // frame is traced from the origin looking down.
        if (mCamera->getUpdateCallback() != nullptr)
            mCamera->accept(*mUpdateVisitor);
    }

    void RtxRenderer::fitToWindow()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(mWindow, &width, &height);

        const std::uint32_t wide = static_cast<std::uint32_t>(std::max(width, 1));
        const std::uint32_t high = static_cast<std::uint32_t>(std::max(height, 1));
        if (wide != mWidth || high != mHeight)
        {
            mWidth = wide;
            mHeight = high;
            mRenderer->resize(mWidth, mHeight);
        }

        // Whatever the backend settled on, which is what the trace and the GUI are both sized to.
        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        mCamera->setViewport(0, 0, static_cast<int>(extents.mOutputWidth), static_cast<int>(extents.mOutputHeight));
    }

    void RtxRenderer::drawGui()
    {
        // **Between the frame and the present**, because the GUI goes over the finished picture and
        // its colours are display-referred — they were picked looking at a monitor, and a tone curve
        // meant for radiance is how a menu comes out grey.
        if (MyGUIRtx::RenderManager* gui = MyGUIRtx::RenderManager::getInstancePtr())
            gui->collectDrawCalls();
    }

    void RtxRenderer::deferRedraw(TracedView& view)
    {
        if (std::find(mDeferred.begin(), mDeferred.end(), &view) == mDeferred.end())
            mDeferred.push_back(&view);
    }

    void RtxRenderer::forgetView(TracedView& view)
    {
        std::erase(mDeferred, &view);

        // Nulled rather than erased: a flush may be walking this, and a view that went away from
        // inside one must not move the elements after it.
        std::replace(mDrawing.begin(), mDrawing.end(), &view, static_cast<TracedView*>(nullptr));
    }

    void RtxRenderer::drawDeferredViews()
    {
        if (mDeferred.empty())
            return;

        mDrawing.swap(mDeferred);
        mDeferred.clear();

        for (TracedView* view : mDrawing)
            if (view != nullptr)
                view->redraw();

        mDrawing.clear();
    }

    void RtxRenderer::renderGui()
    {
        // **The GUI over whatever the target holds**, which is the last frame traced, or black
        // where nothing has been — a main menu, or the moment before the first cell finishes
        // loading. The surface has to be fed either way or the compositor decides the window has
        // stopped answering.
        drawGui();

        if (!mRenderer->presentFrame())
            fitToWindow();
    }

    RtxBridge::TracedFrame RtxRenderer::readFrame()
    {
        const ::Rtx::FrameExtents extents = mRenderer->getExtents();
        if (extents.mOutputWidth == 0 || extents.mOutputHeight == 0)
            return {};

        mRenderer->readPixels(mPixels);

        return RtxBridge::TracedFrame{
            .mWidth = extents.mOutputWidth,
            .mHeight = extents.mOutputHeight,
            .mPixels = mPixels,
        };
    }

    void RtxRenderer::capture(osg::Image& image, int width, int height)
    {
        // An out-parameter because the caller owns the image, so the shared conversion's result is
        // moved into it rather than handed back.
        const osg::ref_ptr<osg::Image> taken
            = RtxBridge::frameImage(readFrame(), width, height, RtxBridge::RowOrder::BottomFirst);
        if (taken == nullptr)
            return;

        image.allocateImage(width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE);
        std::memcpy(image.data(), taken->data(), image.getTotalSizeInBytes());
    }

    void RtxRenderer::saveScreenshot()
    {
        const RtxBridge::TracedFrame frame = readFrame();

        // Bottom row first, because what writes the file is `osgDB` through the same operation the
        // rasterizer hands `osgViewer`'s captures to, and that is the convention it reads.
        const osg::ref_ptr<osg::Image> taken = RtxBridge::frameImage(
            frame, static_cast<int>(frame.mWidth), static_cast<int>(frame.mHeight), RtxBridge::RowOrder::BottomFirst);

        if (taken == nullptr)
        {
            Log(Debug::Warning) << "Ray tracing has no frame to write a screenshot from";
            return;
        }

        // Straight to the writer rather than through a capture handler: the handler's job is to get
        // a frame off the graphics context, and this frame is already off it.
        (*mScreenshotWriter)(*taken, 0);
    }

    std::unique_ptr<OffscreenView> RtxRenderer::createOffscreenView(const OffscreenViewSpec& spec)
    {
        return std::make_unique<TracedView>(spec, *this, *mRenderer);
    }

    MyGUI::ITexture& RtxRenderer::freezeFrame()
    {
        const RtxBridge::TracedFrame frame = readFrame();

        // The trace's own row order, kept: `MyGUIPlatform::Picture` copies an image straight into a
        // locked texture and the interface draws it from the top down, so flipping here would stand
        // the world the player was in on its head behind the loading screen.
        const osg::ref_ptr<osg::Image> taken = RtxBridge::frameImage(
            frame, static_cast<int>(frame.mWidth), static_cast<int>(frame.mHeight), RtxBridge::RowOrder::TopFirst);

        // A full readback, which a load screen is exactly the moment to afford.
        if (taken != nullptr)
            mFrozenFrame.set(*taken);

        if (mFrozenFrame.getTexture() == nullptr)
        {
            // Nothing has been presented yet, which is the very first load. Black is what a fade
            // from nothing looks like, and it is the honest picture of a world that is not there.
            osg::ref_ptr<osg::Image> black = new osg::Image;
            black->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            std::memset(black->data(), 0, black->getTotalSizeInBytes());
            mFrozenFrame.set(*black);
        }

        return *mFrozenFrame.getTexture();
    }

    std::unique_ptr<MyGUIPlatform::Platform> RtxRenderer::createGuiPlatform(osg::Group& guiRoot,
        Resource::ImageManager& images, Shader::ShaderManager& shaders, const VFS::Manager& vfs, float scalingFactor,
        VFS::Path::NormalizedView resourcePath, const std::filesystem::path& logPath)
    {
        // **MyGUI over the ray tracer, and nothing of OpenSceneGraph in it.** `guiRoot` is where the
        // rasterizer hangs its GUI camera; there is no graph to hang anything off here, and the
        // backend is called by this renderer's own frame instead — `updateTraversal` for the widget
        // animation and `renderFrame` for the triangles.
        auto manager = std::make_unique<MyGUIRtx::RenderManager>(*mRenderer, &images, scalingFactor);

        return std::make_unique<MyGUIPlatform::Platform>(std::move(manager), &vfs, resourcePath, logPath);
    }

    void RtxRenderer::keep()
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

    void RtxRenderer::renderFrame(const SceneFrame& frame)
    {
        const osg::FrameStamp& when = frame.mWhen;

        mFrame = when.getFrameNumber();

        // **The world's clock and not this renderer's.** Everything the graph animates under its own
        // controller reads it off the walk's frame stamp, and the sea off the frame's constants; a
        // clock of our own would run both while the game was paused and neither in step with the
        // time of day.
        mExtractor->setSimulationTime(when.getSimulationTime());

        // **Every frame, and the placements are the one thing it does not throw away.** What goes
        // is the lists a walk refills wholesale — lights, deformed meshes, sprites, emitters. The
        // meshes, materials and texture paths stay because the acceleration structures and the
        // texture array were built from them, and the placements stay because they are addressed by
        // slot: a re-walk over an unchanged graph finds every one of them where it left it.
        mScene.clearPlacement();

        const RtxBridge::ExtractionStats found
            // One walk over the whole graph, where every path is already distinct.
            = mExtractor->extract(frame.mScene, osg::Matrixf::identity(), 0, mFrame);

        // **Traced or not, the frame is presented.** A walk that placed nothing and an eye with no
        // roll are both reasons to leave the target holding whatever it last held; neither is a
        // reason to stop feeding the surface. Skipping the present would freeze the window on every
        // frame with no world in it — which is every frame of the main menu — and a window that
        // stops answering is one the compositor eventually says so about.
        const bool traced = traceWorld(frame, found);

        // **The frame the trace made, on the screen, before this call returns.** No composite, no
        // interop and no rasterized frame underneath — which is what takes `.notes/rtx/plan.md` §12's
        // frame of latency out: the image presented is the one just traced.
        drawGui();

        if (!mRenderer->presentFrame())
            fitToWindow();

        // **After the frame and not before the walk.** Where everything stood this frame is what
        // the next one measures its motion against, and saying so any earlier would have this frame
        // comparing itself with itself.
        //
        // On the frames the trace refused as well: the walk still ran, so its epoch is still the
        // one the next walk has to be measured against.
        mExtractor->advance();

        // **What the walk did not find has gone, and this is where the scene is told.** The graph
        // above is the whole world every frame, which is what makes mark and sweep sound here — and
        // it is not only about memory: the identity maps are keyed on `osg` pointers, and an address
        // the engine has freed can come back holding something else entirely.
        //
        // Last, because it bumps the epoch the next walk is measured against: everything that
        // survived is still carrying the old stamp until it does.
        if (const RtxBridge::Retirement went = mExtractor->retire(); !went.empty())
            Log(Debug::Info) << "Ray tracing dropped " << went.mMeshes << " meshes, " << went.mMaterials
                             << " materials and " << went.mTextures << " textures the world no longer has";

        // Only what a trace wrote, because the cap is a count of pictures and not of frames: a run
        // that spent its first sixteen at the main menu would write the same black texel sixteen
        // times and have nothing left for the world.
        if (traced)
            keep();
    }

    bool RtxRenderer::traceWorld(const SceneFrame& frame, const RtxBridge::ExtractionStats& found)
    {
        const osg::FrameStamp& when = frame.mWhen;
        const osg::Camera& camera = frame.mCamera;
        const WorldState& world = frame.mWorld;

        if (mScene.getPlacedCount() == 0)
            return false;

        // Placed, appended or rebuilt — the decision, and the describing a rebuild needs, are the
        // harness's too and are written once (`RtxBridge::SceneUploader`).
        const RtxBridge::SceneUpload handed = mUploader.hand(*mRenderer, mScene, frame.mImages, ::Rtx::SeaState{});

        mHasScene = true;

        if (handed.mKind == RtxBridge::SceneUpload::Kind::Rebuilt)
            Log(Debug::Info) << "Ray tracing built " << mScene.getMeshes().size() << " meshes into " << found.mInstances
                             << " instances with " << found.mLights << " lights, " << found.mDeformed
                             << " of them deforming, and skipped " << found.mSkippedUnknown << " it cannot read";

        if (handed.mUnreadable > 0)
            Log(Debug::Warning) << "Ray tracing could not read " << handed.mUnreadable << " of " << handed.mDescribed
                                << " textures and drew them grey — a live graph holds textures that were never files";

        // **Before the frame and after the scene**, which is the only moment both are true: a
        // picture inside the interface traces against the world this walk has just handed over.
        //
        // Above the eye, because a picture inside the interface brought its own: an eye the trace
        // cannot look along is no reason to leave a map tile blank.
        drawDeferredViews();

        // **In double, and the direction reduced to one before either is narrowed.** OSG hands back
        // a point one unit ahead of the eye, and Morrowind's cells are far enough out that a float
        // ulp there is a hundredth of a unit: differencing two such points names a direction a fifth
        // of a degree wide, and it lands somewhere else every time the eye moves. Where the eye is
        // survives the narrowing — a hundredth of a unit is a fifth of a millimetre — but which way
        // it faces does not.
        osg::Vec3d at;
        osg::Vec3d ahead;
        osg::Vec3d skyward;
        camera.getViewMatrixAsLookAt(at, ahead, skyward);

        osg::Vec3d direction = ahead - at;
        direction.normalize();

        const osg::Vec3f eye(at);
        const osg::Vec3f forward(direction);

        if (!canLookAlong(forward))
        {
            // Once, because a frame the tracer cannot draw is usually a cutscene and occasionally a
            // camera nobody has filled in — and the two look identical from here until it is said
            // out loud how often it happens.
            if (!mComplained)
            {
                mComplained = true;
                Log(Debug::Warning) << "Ray tracing skipped a frame: the camera at " << eye.x() << ", " << eye.y()
                                    << ", " << eye.z() << " looks along " << forward.x() << ", " << forward.y() << ", "
                                    << forward.z();
            }
            return false;
        }

        const ::Rtx::FrameExtents extents = mRenderer->getExtents();

        // **The frame's field of view and not the setting's.** `WorldState` carries the one the
        // world settled on, which is the override wherever something asked for one — a zoom, a
        // cutscene, a script — and the setting only where nothing did.
        ::Rtx::Shaders::VisibilityConstants constants = ::Rtx::makeCameraAlong(
            eye, forward, world.mFieldOfView, extents.mRenderWidth, extents.mRenderHeight, sFar);
        // **Decoded here, because the world does not know what a transport is.** Every colour on
        // the frame is a content file's three bytes over 255 and no transfer function; the
        // rasterizer samples them as they are and this light transport is linear, so the conversion
        // belongs to whichever renderer needs it. The harness runs the same decode on the same
        // numbers, which is what keeps a screenshot and the game one picture.
        osg::Vec3f sun(world.mSunVector.x(), world.mSunVector.y(), world.mSunVector.z());
        if (sun.length2() > 0.0f)
            sun.normalize();

        const osg::Vec3f haze = RtxBridge::decodeColour(world.mAir.mColour);

        constants.mSunDirection = sun;

        // Scaled by the same ratio of sun to sky the harness uses. Sharing the constant is what
        // keeps a screenshot and the game the same picture.
        constants.mSunIrradiance = RtxBridge::decodeColour(world.mSunColour) * ::Rtx::Shaders::DAYLIGHT;
        constants.mAmbient = RtxBridge::decodeColour(world.mAmbientColour);

        // Negative infinity and not zero: zero is sea level, and a cell with no water has to answer
        // "how deep is this point" with never.
        constants.mWaterLevel = world.mWaterEnabled ? world.mWaterHeight : -std::numeric_limits<float>::infinity();

        // **What the sea is animated by, and leaving it at zero is a frozen ocean.** The harness
        // passes this through `applyLighting`; the game assembles its own constants and simply did
        // not, so every wave stood still. Real elapsed seconds rather than the frame count: a sea
        // that ran at the frame rate would slow down whenever the frame did.
        constants.mTime = static_cast<float>(when.getSimulationTime());

        // The horizon is the fog and the zenith is the sky's own, which is the pair Morrowind
        // records: one colour for the air, and one for the dome it fades into overhead.
        constants.mSkyHorizon = haze;

        // **The sky's own colour, and an interior has none.** The weather system stops writing it
        // the moment the player steps inside, so what the sky is still holding belongs to wherever
        // they were last outdoors — and the air's own colour stands in, which is what a room's sky
        // is anyway. A quasi-exterior is on the outdoor side of that: it has weather.
        constants.mSkyZenith = world.isOutdoors() ? RtxBridge::decodeColour(world.mSkyColour) : haze;

        constants.mFogColour = haze;

        // **The fog is a linear ramp there and a medium here**, so what is matched is where each is
        // half gone: the ramp at the midpoint of start and end, an exponential at `ln(2) / sigma`.
        // The same derivation `RtxBridge::fogExtinction` makes from a recorded depth, reached
        // instead from the distances the game has already computed.
        const float half = 0.5f * (world.mAir.mStart + world.mAir.mEnd);
        constants.mFogExtinction = half > 0.0f ? std::numbers::ln2_v<float> / half : 0.0f;

        // **What the sampler and the jitter are walked by, and leaving it at zero is a bug with two
        // faces.** The bounce samples the same point every frame, so nothing ever converges; and the
        // upscaler, which jitters whatever it is told, is handed the same sub-pixel offset every
        // frame and reconstructs from one sample taken repeatedly. The harness had exactly this, and
        // it cost a picture that looked plausible and carried none of the detail it was paying for.
        constants.mFrame = static_cast<std::uint32_t>(mFrame);

        // **Measured, not held at one.** A picture wants the exposure the frame asks for; holding
        // it is what a reference and a pixel test want, and the default is theirs. Without this an
        // interior lit by nothing but this placeholder's ambient reaches the screen at a few
        // hundredths and reads as black.
        const ::Rtx::FrameResult result
            = mRenderer->renderFrame(constants, ::Rtx::FrameOptions{ .mExposure = std::nullopt });

        // **The whole frame, measured between one trace and the next.** Everything the game does
        // in between is in it — update, cull, the rasterizer, this — which is what a player feels
        // and what `result.mTraceMs` on its own cannot say.
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (mEnteredOnce)
            mBench.frame(result, std::chrono::duration<double, std::milli>(now - mEntered).count());

        mEntered = now;
        mEnteredOnce = true;

        mSpentMs += result.mTraceMs;
        if (++mTimed == sReportEvery)
        {
            Log(Debug::Info) << "Ray tracing: " << mSpentMs / mTimed << " ms a frame over the last " << mTimed
                             << ", tracing " << mScene.getPlacedCount() << " instances at " << extents.mRenderWidth
                             << "x" << extents.mRenderHeight;
            mSpentMs = 0.0;
            mTimed = 0;
        }

        return true;
    }
}
