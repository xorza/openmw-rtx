#include "rtxrenderer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <ratio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <MyGUI_ITexture.h>
#include <SDL3/SDL_video.h>
#include <osg/Camera>
#include <osg/FrameStamp>
#include <osg/GL>
#include <osg/Group>
#include <osg/Image>
#include <osg/Matrixf>
#include <osg/Node>
#include <osg/Stats>
#include <osg/Texture2D>
#include <osg/Timer>
#include <osg/Vec2i>
#include <osg/Vec3f>

#include <apps/openmw/mwbase/environment.hpp>
#include <apps/openmw/mwbase/windowmanager.hpp>
#include <apps/openmw/mwrender/ground.hpp>
#include <apps/openmw/mwrender/mapoverlay.hpp>
#include <apps/openmw/mwrender/offscreenview.hpp>
#include <apps/openmw/mwrender/sceneframe.hpp>
#include <apps/openmw/mwrender/skystate.hpp>
#include <apps/openmw/mwrender/vismask.hpp>
#include <apps/openmw/mwworld/esmstore.hpp>
#include <apps/openmw/mwworld/ptr.hpp>
#include <components/contentstore/store.hpp>
#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashinstall.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/loadinglistener/loadinglistener.hpp>
#include <components/misc/frameclock.hpp>
#include <components/misc/presentation.hpp>
#include <components/myguiplatform/myguiplatform.hpp>
#include <components/myguirtx/rendermanager.hpp>
#include <components/platform/process.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/rtx/common/clock.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/common/namedenum.hpp>
#include <components/rtx/frame/camera.hpp>
#include <components/rtx/frame/frameextents.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/mirror/poseupdate.hpp>
#include <components/rtx/preprocess/preparedimages.hpp>
#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/renderer/framespend.hpp>
#include <components/rtx/renderer/renderer.hpp>
#include <components/rtx/renderer/sceneuploader.hpp>
#include <components/rtx/renderer/shaderdirectory.hpp>
#include <components/rtx/renderer/verticalsync.hpp>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtx/world/moon.hpp>
#include <components/sceneutil/screencapture.hpp>
#include <components/sdlutil/imagetosurface.hpp>
#include <components/sdlutil/vsyncmode.hpp>
#include <components/settings/categories.hpp>
#include <components/settings/values.hpp>
#include <components/shader/automaps.hpp>
#include <components/vfs/pathutil.hpp>

#include "classmasks.hpp"
#include "contentmodels.hpp"
#include "rtxsettings.hpp"
#include "rtxsupport.hpp"
#include "tracedground.hpp"
#include "tracedoverlay.hpp"
#include "tracedview.hpp"
#include "worldmirror.hpp"

namespace MWRender
{
    namespace
    {
        /// The game's vertical sync in the core's words, which the window layer's enum is not.
        Rtx::VerticalSync verticalSyncOf(const SDLUtil::VSyncMode mode)
        {
            switch (mode)
            {
                case SDLUtil::VSyncMode::Disabled:
                    return Rtx::VerticalSync::Off;
                case SDLUtil::VSyncMode::Enabled:
                    return Rtx::VerticalSync::On;
                case SDLUtil::VSyncMode::Adaptive:
                    return Rtx::VerticalSync::Adaptive;
            }
            Crash::fatal("a vertical sync mode past the three");
        }

    }

    RtxRenderer::RtxRenderer(const RendererSpec& spec, const RtxSetup* const run)
        : RtxRenderer(spec, run, run != nullptr ? run->mSetup : playedRunSetup(spec.mCachePath))
    {
    }

    RtxRenderer::RtxRenderer(const RendererSpec& spec, const RtxSetup* const run, const RunSetup& setup)
        : mRun(run != nullptr ? run->mRun : mPlayed)
        , mInterface(setup.mInterface)
        , mKernels(setup.mKernels)
        , mWindow(RtxWindow::hasDisplay() ? std::optional(surfaceFlagsOf(setup.mBackend)) : std::nullopt,
              setup.mHeadless, setup.mWindowPixels)
        , mUpdateVisitor(new Rtx::PoseUpdate)
        , mStartTick(osg::Timer::instance()->tick())
        , mContentStore(setup.mContentStore.has_value()
                  ? std::make_unique<ContentStore::Store>(*setup.mContentStore, Rtx::sContentStorePolicy)
                  : nullptr)
        , mPrepared(std::make_unique<Rtx::PreparedImages>(
              mContentStore.get(), std::max<std::size_t>(1, Platform::Process::usableCpuCount() / 2)))
        , mMirror(setup.mMirror, mContentStore.get(), mPrepared.get())
    {
        mSettled = setup.mSettled;

        // **Made here, because there is no viewer to make them.** Every renderer needs the four and
        // one built on `osgViewer` gets them already wired together.
        const osg::ref_ptr<osg::Camera> camera = new osg::Camera;
        const osg::ref_ptr<osg::FrameStamp> frameStamp = new osg::FrameStamp;
        const osg::ref_ptr<osg::Stats> stats = new osg::Stats("Viewer");

        frameStamp->setFrameNumber(0);
        frameStamp->setReferenceTime(0.0);
        frameStamp->setSimulationTime(0.0);
        mUpdateVisitor->setFrameStamp(frameStamp);

        adopt(*camera, *frameStamp, *stats);

        Rtx::RendererOptions options;
        options.mShaders
            = setup.mShaders.value_or(Rtx::ShaderSet{ .mDirectory = Rtx::shaderDirectory(spec.mResourceDir) });

        // A measured run keeps no pipeline cache of its own: a pipeline out of the blob starts on the compile's
        // first code and is swapped for the driver's second (`RtxTool::DriverCache`), and the object shared
        // between parallel compiles handed one pipeline another's code (`PipelineCacheSpec::mDirectory`).
        // The player keeps the fork's cache: a game is not compared with itself.
        if (run == nullptr)
            options.mCacheDirectory = spec.mCachePath;
        options.mDlss = spec.mDlss;
        // None whose traced extent truncates to nought, and none past the side every device chosen
        // makes an image at.
        constexpr auto smallest = static_cast<int>(Rtx::sSmallestFrameSide);
        constexpr auto largest = static_cast<int>(Rtx::sLargestFrameSide);
        boundFrame(
            Misc::FrameBounds{ .mSmallest = osg::Vec2i(smallest, smallest), .mLargest = osg::Vec2i(largest, largest) });
        presentIn(mWindow.readSize());
        options.mWidth = static_cast<std::uint32_t>(getPresentation().mFrame.x());
        options.mHeight = static_cast<std::uint32_t>(getPresentation().mFrame.y());
        options.mWindow = mWindow.hasSurface() ? mWindow.get() : nullptr;
        options.mVerticalSync = verticalSyncOf(Settings::video().mVsyncMode);
        // Handed over whole, so a picture taken by the harness and a frame drawn by the game come from one
        // configuration. `playedRunSetup` says what a session with no command line answers.
        options.mRun = setup.mRun;
        options.mRun.mValidation = askedValidation(setup.mRun.mValidation);

        // Measured exactly where a run is installed: the hit counts, zone timestamps, digest and pipeline
        // reports are read by nothing a player does, so a played session is specialized without the atomics
        // and makes neither the digest's pass nor its buffers.
        options.mReporting = run != nullptr ? Rtx::Reporting::Measured : Rtx::Reporting::Played;

        // Said once: none of it changes while the session runs but the upscale, which the line where the
        // targets are made says.
        Log(Debug::Info) << "Ray tracing run: " << describe(setup);

        // A switch the player turned on for what this renderer declines is said, with the declaration's
        // reason, and not refused: the game plays the same without it.
        const std::pair<Settings::CategorySetting, bool> switches[] = {
            { { "Stereo", "stereo enabled" }, Settings::stereo().mStereoEnabled },
            { { "Post Processing", "enabled" }, Settings::postProcessing().mEnabled },
        };
        for (const auto& [setting, on] : switches)
            if (const std::string_view declined = rtxSupport().declinedSetting(setting.first, setting.second);
                on && !declined.empty())
                Log(Debug::Warning) << "[" << setting.first << "] " << setting.second << " is on: " << declined;

        mRenderer = createBackend(setup.mBackend, options);
        mUpscale = setup.mRun.mProfile.mUpscale;

        // What `auto` came to is the device's description's `upscaler` line.
        Log(Debug::Info) << "Ray tracing on " << mRenderer->describeDevice();

        // A driver that takes a crash signal for itself as it starts leaves the crash catcher blind to every
        // crash after it.
        for (const std::string_view lost : Crash::lostHooks())
            Log(Debug::Warning) << "The crash catcher no longer handles " << lost
                                << ": something the renderer started replaced its handler";

        mWindow.apply(*mRenderer, getCamera(), getPresentation());

        // Nothing above may have made a GL context (the window, a realize operation, a stray `osgViewer`):
        // one that exists is one something is paying for.
        if (SDL_GL_GetCurrentContext() != nullptr)
            throw std::runtime_error("something initialised OpenGL under the ray tracing renderer");
    }

    Rtx::CardAddress RtxRenderer::getCardAddress() const
    {
        return mRenderer->getCardAddress();
    }

    Rtx::UpscalerInput RtxRenderer::getUpscalerInput() const
    {
        return mRenderer->getUpscalerInput();
    }

    Rtx::FrameExtents RtxRenderer::getExtents() const
    {
        return mRenderer->getExtents();
    }

    // Out of line because the members it destroys are only forward declared in the header.
    RtxRenderer::~RtxRenderer()
    {
        // `Engine` has stopped the screenshot writer by now, so no write on the queue still holds
        // an image of a frame this owns the memory for. The members go in the order they are
        // declared for: the frozen texture, the backend, the window.

        // Failures are counted and said once here, not each logged: a drop is a store that missed what it
        // would have kept.
        Log(Debug::Info) << "Frame times of the session: " << Rtx::describe(mSessionTimes.summarise());

        if (mContentStore != nullptr)
        {
            const ContentStore::Stats store = mContentStore->getStats();
            Log(Debug::Info) << "Rtx: the content store found " << store.mFound << " entries and missed "
                             << store.mMissed << ", held " << store.mHeld << " of " << store.mHeld + store.mUnheld
                             << " asked whether it held them; wrote " << store.mWritten << ", dropped "
                             << store.mDropped << ", refused " << store.mRefused << ", stamped " << store.mStamped
                             << " and swept " << store.mStale << " temporaries";
        }
    }

    void RtxRenderer::updateEye(osg::Camera& camera, osgUtil::UpdateVisitor& visitor)
    {
        if (camera.getUpdateCallback() == nullptr)
            return;

        const osg::NodeVisitor::TraversalMode was = visitor.getTraversalMode();
        visitor.setTraversalMode(osg::NodeVisitor::TRAVERSE_NONE);
        camera.accept(visitor);
        visitor.setTraversalMode(was);
    }

    void RtxRenderer::onDetachWorld() noexcept
    {
        // No frame phase expected: the world goes on the way out of an exception a frame threw,
        // and the assert would stand between the throw and its message.
        mSky.detach(mMirror.getScene());
        mMirror.detach();
        mRipples.clear();
        mWorldRoot = nullptr;
    }

    float RtxRenderer::getGroundReach() const noexcept
    {
        return mMirror.getReach();
    }

    void RtxRenderer::configureResources(Resource::ResourceSystem& resources) noexcept
    {
        mPhase.expect(Phase::Between);
        const std::optional<float> stated = getLoopClock().getStatedStep();
        setResourceExpiry(resources, stated);

        // A stated step makes the ground wait: a composite lands on whichever frame the baker finishes it
        // on, a thread's answer a run compared with another's cannot have. The step and not a hashed frame
        // decides, because `shot` makes the reference pictures and hashes none (measured on `balmora`, four
        // processes drew four different frames after half a second of warming). A run timing the streaming
        // path overrides it, since waiting is most of what that path measures (`RunSetup::mSettled`).
        mMirror.getRing().setSettled(mSettled.value_or(stated.has_value()));

        Resource::SceneManager& scene = *resources.getSceneManager();
        scene.setModelPrograms(Resource::ModelPrograms::Left);
        setAutoMaps(scene);
    }

    void RtxRenderer::setAutoMaps(Resource::SceneManager& scene)
    {
        Shader::AutoMapRules maps = scene.getAutoMaps();
        maps.mNormalMaps = true;
        maps.mSpecularMaps = true;
        scene.setAutoMaps(maps);
    }

    void RtxRenderer::setResourceExpiry(Resource::ResourceSystem& resources, const std::optional<float>& step)
    {
        if (step.has_value())
            resources.setExpiryDelay(std::numeric_limits<double>::infinity());
    }

    osg::ref_ptr<osg::Group> RtxRenderer::createSceneRoot() noexcept
    {
        mPhase.expect(Phase::Between);
        mSceneRoot = new osg::Group;
        return mSceneRoot;
    }

    std::unique_ptr<Ground> RtxRenderer::createGround(const GroundSpec& spec) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        if (spec.mWorldspace == ESM::Cell::sDefaultWorldspaceId)
            mMirror.growGroundcover(spec.mGroundcoverStore);

        return std::make_unique<TracedGround>(spec, *getResources().getSceneManager(), Mask_Terrain, mMirror);
    }

    void RtxRenderer::addCell(const MWWorld::CellStore* cell) noexcept
    {
        expectWorldEvent();
        mMirror.standSea(*cell);
    }

    void RtxRenderer::removeCell(const MWWorld::CellStore* cell) noexcept
    {
        expectWorldEvent();
        mRipples.removeCell(*cell);
    }

    void RtxRenderer::addWaterRippleEmitter(const MWWorld::Ptr& ptr) noexcept
    {
        expectWorldEvent();
        mRipples.add(ptr);
    }

    void RtxRenderer::removeWaterRippleEmitter(const MWWorld::Ptr& ptr) noexcept
    {
        expectWorldEvent();
        mRipples.remove(ptr);
    }

    void RtxRenderer::emitWaterRipple(const osg::Vec3f& position) noexcept
    {
        expectWorldEvent();
        mRipples.splash(position);
    }

    void RtxRenderer::poseForIntersection(osg::Drawable& drawable) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        mPoser.pose(drawable, getFrameStamp());
    }

    void RtxRenderer::listAssetsToPreload(
        std::vector<VFS::Path::Normalized>& models, std::vector<VFS::Path::Normalized>& textures) noexcept
    {
        mPhase.expect(Phase::Between);
        SkyReader::listAssets(*getResources().getVFS(), models, textures);
    }

    void RtxRenderer::functionKey(const int index, const SDLUtil::KeyChange change)
    {
        mPhase.expect(Phase::Between, Phase::Run);
        // F3 and F4: the indices count from F1.
        if (change != SDLUtil::KeyChange::Pressed || (index != 2 && index != 3))
            return;

        MWBase::Environment::get().getWindowManager()->messageBox(
            notAvailable("The statistics overlay", support().declinedRequest(PictureRequest::StatsOverlay)));
    }

    void RtxRenderer::windowResized(int, int, const int width, const int height) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        presentIn(osg::Vec2i(width, height));
    }

    void RtxRenderer::onAttachWorld(RenderingManager&, osg::Group& worldRoot, SceneUtil::UnrefQueue& released) noexcept
    {
        mPhase.expect(Phase::Between);
        // Straight under the root: the rasterizer hangs its shadowed scene between the two, and
        // this renderer has nothing to put there. The root is kept for what the game hangs on it
        // beside the scene: its debug nodes, which every frame reads off it.
        assert(mSceneRoot != nullptr && "the world is built under a root this renderer made");
        worldRoot.addChild(std::exchange(mSceneRoot, nullptr));
        mWorldRoot = &worldRoot;

        mMirror.attach(getResources(), released);

        // The sky's sheets into the mirror's scene, once: they are drawn by rays that reach
        // nothing, so nothing the walk finds would keep their slots.
        mSky.attach(mMirror.getScene(), *getResources().getSceneManager(), mMirror.getWalkContext().mContent);
    }

    void RtxRenderer::adoptTraversalRoot(osg::Group& root) noexcept
    {
        mPhase.expect(Phase::Between);
        // Under the camera, whose matrices are what put a viewport ray in the world; parented once
        // however often it is said.
        osg::Camera& camera = getCamera();
        if (!camera.containsNode(&root))
            camera.addChild(&root);
    }

    void RtxRenderer::advance(double simulationTime) noexcept
    {
        mPhase.expect(Phase::Between);
        getFrameStamp().setFrameNumber(getFrameStamp().getFrameNumber() + 1);

        // What OpenMW ages its caches by, so it comes from the host's clock and not the wall.
        getFrameStamp().setReferenceTime(getLoopClock().getNow());
        getFrameStamp().setSimulationTime(simulationTime);
    }

    void RtxRenderer::updateTraversal() noexcept
    {
        mPhase.expect(Phase::Between);
        // Before the early return, because a main menu has no scene root: MyGUI's animation, key repeat,
        // tooltip timers and faders all hang off this call. The frame's own step and not MyGUI's timer, a
        // wall clock in whole milliseconds that made two runs draw a hit's red overlay at different strengths.
        assert(mGui != nullptr && "a frame before the interface was made");
        mGui->update(static_cast<float>(getLoopClock().getStep()));

        mUpdateVisitor->reset();
        mUpdateVisitor->setFrameStamp(&getFrameStamp());
        mUpdateVisitor->setTraversalNumber(getFrameStamp().getFrameNumber());

        // What the rasterizer says with a blanked traversal mask this says by not walking; the eye below
        // still updates, as it does under that mask. `tws` is not asked: under the rasterizer it masks the
        // cull alone and the world keeps animating behind it.
        if (isWorldShown())
            getTraversalRoot().accept(*mUpdateVisitor);

        // The eye is not in the graph: `MWRender::Camera` sets the master camera from an update callback,
        // and without this every frame is traced from the origin looking down.
        updateEye(getCamera(), *mUpdateVisitor);
    }

    void RtxRenderer::drawGui()
    {
        // Over the finished picture, not in the trace: its colours are display-referred, and a tone curve
        // meant for radiance turns a menu grey.
        assert(mGui != nullptr && "a GUI drawn before the interface was made");
        if (mInterface)
            mGui->collectDrawCalls();
    }

    FrameContext RtxRenderer::describeContext()
    {
        return FrameContext{
            .mRenderer = *this,
            .mMirror = mMirror,
            .mBackend = *mRenderer,
            .mResources = getResources(),
        };
    }

    double RtxRenderer::drawViews()
    {
        mPhase.expect(Phase::Views, Phase::Run);
        assert(!mViews.isDrawing() && "drawViews inside drawViews");

        // A cell asks for its map tile as it loads, the frame before the one that first mirrors it; the
        // tile waits as a deferred view rather than staying blank until the local map asks again.
        if (!mViews.hasDeferred())
            return 0.0;

        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
        mViews.draw(sWorldViewsPerFrame, getFrameStamp(), mMirror.getRing(), mPictureReleases);
        mMirror.release(mPictureReleases);
        return Rtx::since(began, std::chrono::steady_clock::now());
    }

    void RtxRenderer::renderGui() noexcept
    {
        // From between two frames alone, which is a loading screen presenting: a frame presents
        // through its own exits, and a hook that presented from inside one presented it twice.
        mPhase.step(Phase::Gui, Phase::Between);
        present();
    }

    /// The frame the trace made, on the screen before the call that made it returns: no composite or
    /// interop, which is what takes an interop path's frame of latency out. Traced or not, the frame is
    /// presented: leaving the target as it is is fine, but a surface that stops being fed gets the window
    /// reported as unresponsive. The GUI then goes over the last frame traced, or black where none was.
    void RtxRenderer::present()
    {
        mPhase.expect(Phase::Gui);
        const Crash::NoteScope noted("drawing the interface and presenting");

        const std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

        drawGui();

        // A failed present is a swapchain `renderFrame` rebuilds on the next frame, once the window's size
        // has settled. None where the window has no surface (`RtxWindow::hasDisplay`): the interface is
        // drawn into the frame all the same, which a screenshot, a frozen frame and a loading screen read.
        if (mWindow.hasSurface())
            mRenderer->presentFrame();

        // Summed and not assigned: a loading screen presents through `renderGui` as often as it
        // likes between two traces, and every one of those is inside the frame the next row is for.
        const std::chrono::steady_clock::time_point ended = std::chrono::steady_clock::now();
        mTimer.addPresent(Rtx::since(began, ended));

        mTimer.leave(ended);
        mPhase.step(Phase::Between, Phase::Gui);
    }

    void RtxRenderer::awaitShaders(Loading::Listener& listener)
    {
        mPhase.expect(Phase::Between, Phase::Run);

        // Content is prepared beside the kernels' compile, so play reads from the store what it would
        // otherwise compute (`Rtx::ContentPreparation`). Every CPU but the main thread's, which draws the
        // screen and sleeps: 64 s on 31 threads against 80 on 16
        // (`docs/rtx/research/2026-11-09-content-preparation-cost.md`, "Threads").
        const MWWorld::ESMStore* const store = MWBase::Environment::get().getESMStore();
        if (store != nullptr)
            mMirror.startPreparing(getResources(), contentModels(*store, getResources()),
                contentLayers(*store, getResources()),
                std::max<std::size_t>(1, Platform::Process::usableCpuCount() - 1));

        Rtx::JobProgress progress = mRenderer->awaitKernels(mKernels, std::chrono::milliseconds::zero());
        if (!progress.isDone())
        {
            listener.setLabel("#{OMWEngine:CompilingShaders}");
            listener.setProgressRange(progress.mCount);

            // Under a frame of the loading screen's, so the screen paces it and not this wait. Every report
            // draws, the count moved or not: the draw pumps the window's events and beats the crash catcher's
            // heart, and one kernel alone takes seconds cold.
            constexpr std::chrono::milliseconds patience{ 8 };
            listener.setProgress(progress.mMade);
            while (!progress.isDone())
            {
                progress = mRenderer->awaitKernels(mKernels, patience);
                listener.setProgress(progress.mMade);
            }
        }

        progress = mMirror.awaitPreparing(std::chrono::milliseconds::zero());
        if (!progress.isDone())
        {
            listener.setLabel("#{OMWEngine:PreparingContent}");
            // A cold start waits here up to minutes, 364 s on a large mod list, and a player told nothing
            // takes it for a hang (`docs/rtx/research/2026-11-09-content-preparation-cost.md`).
            listener.setNote("#{OMWEngine:PreparingContentNote}");
            listener.setProgressRange(progress.mCount);

            // Each file is let go of by the loader's caches as the last job reading it ends
            // (`Rtx::ContentPreparation`, `Resource::SceneManager::letGo`): cleared once a second the caches
            // held 7.3 GiB at the peak of a PBR start, and aged by their expiry, 14.7 GB.
            constexpr std::chrono::milliseconds patience{ 8 };
            // Each report draws a frame, as above, the last one the whole count.
            listener.setProgress(progress.mMade);
            while (!progress.isDone())
            {
                progress = mMirror.awaitPreparing(patience);
                listener.setProgress(progress.mMade);
            }
            listener.setNote({});
        }
        mWalked.mSession.mLoading += mMirror.finishPreparing();

        // Drops what a job's files did not name, so the models a run keeps do not hang on the threads' timing.
        if (store != nullptr)
            getResources().clearCache();
    }

    osg::ref_ptr<osg::Image> RtxRenderer::readFrame(const int width, const int height, const Rtx::Channels channels)
    {
        // A readback drains the queue, which a frame may not pay for and a stop or a loading
        // screen may.
        mPhase.expect(Phase::Between, Phase::Run);

        const Rtx::FrameExtents extents = mRenderer->getExtents();
        if (extents.mOutputWidth == 0 || extents.mOutputHeight == 0)
            return nullptr;

        mRenderer->readPixels(mReadBack);

        const Rtx::TracedFrame frame{
            .mWidth = extents.mOutputWidth,
            .mHeight = extents.mOutputHeight,
            .mPixels = mReadBack,
        };

        return Rtx::frameImage(frame, width > 0 ? width : static_cast<int>(frame.mWidth),
            height > 0 ? height : static_cast<int>(frame.mHeight), Rtx::RowOrder::BottomFirst, channels);
    }

    void RtxRenderer::capture(osg::Image& image, int width, int height) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        const osg::ref_ptr<osg::Image> taken = readFrame(width, height, Rtx::Channels::Rgb);
        if (taken == nullptr)
            return;

        image.swap(*taken);
    }

    void RtxRenderer::saveScreenshot() noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        // Three channels, as the rasterizer reads its screenshots: the JPEG writer `screenshot
        // format` may name refuses four (`Rtx::Channels::Rgb`).
        const osg::ref_ptr<osg::Image> taken = readFrame(0, 0, Rtx::Channels::Rgb);
        if (taken == nullptr)
        {
            Log(Debug::Warning) << "Ray tracing has no frame to write a screenshot from";
            return;
        }

        getScreenshotWriter()(*taken, 0);
    }

    std::unique_ptr<TracedView> RtxRenderer::traceView(const OffscreenViewSpec& spec, const ViewKind kind)
    {
        assert(mGui != nullptr && "a view before the interface was made");
        return std::make_unique<TracedView>(spec, kind, *mRenderer, mViews, *mGui, mMirror.getWalkContext());
    }

    std::unique_ptr<OffscreenView> RtxRenderer::createWorldView(const OffscreenViewSpec& spec) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        return traceView(spec, ViewKind::World);
    }

    std::unique_ptr<SubjectView> RtxRenderer::createSubjectView(const OffscreenViewSpec& spec) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        return traceView(spec, ViewKind::Subject);
    }

    std::unique_ptr<MapOverlay> RtxRenderer::createMapOverlay(const MapOverlaySpec& spec) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        assert(mGui != nullptr && "an overlay before the interface was made");
        return std::make_unique<TracedOverlay>(spec, mViews, *mGui);
    }

    void RtxRenderer::setVSync(SDLUtil::VSyncMode mode) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        mPending.mVerticalSync = verticalSyncOf(mode);
    }

    void RtxRenderer::awaitPresented() noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        mRenderer->awaitPresented();
    }

    const RenderSupport& RtxRenderer::support() const noexcept
    {
        return rtxSupport();
    }

    std::span<const Rtx::UpscalerChoice> RtxRenderer::getUpscalers() const noexcept
    {
        return mRenderer->getUpscalers();
    }

    void RtxRenderer::applyChangedSettings(const Settings::CategorySettingVector& changed) noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        const bool upscale = changed.contains({ "RTX", "upscale" });
        const bool indirect = changed.contains({ "RTX", "indirect lighting" });
        const bool bounceRays = changed.contains({ "RTX", "bounce rays" });
        const bool reach
            = changed.contains({ "RTX", "distant land cells" }) || changed.contains({ "Camera", "viewing distance" });
        const bool anisotropy = changed.contains({ "General", "anisotropy" });
        const bool gamma = changed.contains({ "Video", "gamma" });
        if (!upscale && !indirect && !bounceRays && !reach && !anisotropy && !gamma)
            return;
        const Crash::NoteScope noted("applying the settings the menu changed");

        // What asks is somebody choosing from a menu, so a value the derivation refuses is reported
        // and everything is left where it was.
        std::optional<RtxSettings> settings;
        try
        {
            settings = RtxSettings::derive(RtxSettingValues::fromRegistry());
        }
        catch (const Rtx::InputError& refused)
        {
            Log(Debug::Warning) << "Ray tracing kept the settings it had: " << refused.what();
            return;
        }

        if (upscale)
            mUpscale = settings->mUpscale;

        // The ring, the air and the map follow the reach while the game runs. Handed over here and never
        // read by a frame, so every part of a frame stands in one world.
        if (reach)
            mMirror.setReach(settings->mMirror.mReach);

        if (anisotropy)
            mPending.mAnisotropy = settings->mAnisotropy;
        if (gamma)
            mPending.mGamma = settings->mGamma;
        if (indirect)
            mPending.mIndirectLighting = settings->mIndirectLighting;
        if (bounceRays)
            mPending.mBounceRays = settings->mBounceRays;
    }

    MyGUI::ITexture& RtxRenderer::freezeFrame() noexcept
    {
        mPhase.expect(Phase::Between, Phase::Run);
        const osg::ref_ptr<osg::Image> taken = readFrame();

        if (mFrozenFrame == nullptr)
            mFrozenFrame = new osg::Texture2D;

        // A full readback, which a load screen is exactly the moment to afford.
        if (taken != nullptr)
            mFrozenFrame->setImage(taken);

        if (mFrozenFrame->getImage() == nullptr)
        {
            // Nothing has been presented yet, which is the very first load: black is what a fade from nothing
            // looks like.
            osg::ref_ptr<osg::Image> black = new osg::Image;
            black->allocateImage(1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE);
            std::memset(black->data(), 0, black->getTotalSizeInBytes());
            mFrozenFrame->setImage(black);
        }

        assert(mGui != nullptr && "a frame frozen before the interface was made");
        if (mFrozenFrameTexture == nullptr)
            mFrozenFrameTexture = mGui->shareTexture(*mFrozenFrame);

        return *mFrozenFrameTexture;
    }

    std::unique_ptr<MyGUIPlatform::Platform> RtxRenderer::createGuiPlatform(
        float scalingFactor, VFS::Path::NormalizedView resourcePath, const std::filesystem::path& logPath) noexcept
    {
        // Nothing is hung in the graph: this renderer's own frame calls the backend, `updateTraversal` for the
        // widget animation and `renderFrame` for the triangles.
        auto manager
            = std::make_unique<MyGUIRtx::RenderManager>(*mRenderer, getResources().getImageManager(), scalingFactor);
        mGui = manager.get();

        return std::make_unique<MyGUIPlatform::Platform>(
            std::move(manager), getResources().getVFS(), resourcePath, logPath);
    }

    void RtxRenderer::notifyCut() noexcept
    {
        expectWorldEvent();
        // How the stretch the cut ends ran, which a player's report reads beside where it went.
        Log(Debug::Info) << "Frame times since the last cut: " << Rtx::describe(mStretchTimes.summarise());
        mStretchTimes.clear();
        // Told rather than worked out: the mirror recycles its slots and is never cleared, so a cell load
        // looks like a step across a room. Kept for the next frame the world is traced in.
        mLoss = std::max(mLoss, Rtx::HistoryLoss::Cut);
    }

    void RtxRenderer::notifyWorldspaceChanged() noexcept
    {
        expectWorldEvent();
        mLoss = Rtx::HistoryLoss::Worldspace;
        mRipples.dropStrikes();
        mViews.leaveScene();
    }

    void RtxRenderer::renderFrame(const SceneFrame& frame)
    {
        mPhase.step(Phase::Walking, Phase::Between);

        // Ends with the frame: a crash in the game's update after it names no step of a frame already drawn.
        const Crash::NoteScope noted("drawing frame {}", frame.mWhen.getFrameNumber());

        FrameReport report{ .mPaused = frame.mPaused };

        // The game's update since this renderer last let go of the frame, which nothing else measures and is
        // timed rather than profiled because most of it is a thread asleep. The whole frame is measured from
        // one arrival here to the next, as a player feels it. Entered here and not at the trace, so a frame
        // hidden, with nothing placed, or refused by `describeTrace` closes a span of its own instead of
        // the first traced frame after it reporting the whole gap.
        const std::chrono::steady_clock::time_point arrived = std::chrono::steady_clock::now();
        report.mSpend.at(Rtx::Timing::Update) = mTimer.sinceLeft(arrived);
        report.mSpend.at(Rtx::Timing::Sleep) = std::chrono::duration<double, std::milli>(getLastHold()).count();
        const std::optional<double> since = mTimer.enter(arrived);
        if (since.has_value() && !frame.mPaused)
        {
            mStretchTimes.add(*since);
            mSessionTimes.add(*since);
        }

        // The sky's own clock, stepped where the game stepped the dome's: every unpaused frame the
        // sky is on, whether or not this one is drawn.
        if (!frame.mPaused && frame.mWorld.mSkyShown)
            mSky.step(frame.mDeltaTime, frame.mWorld.mTimeScale, frame.mSky.mWeather.mCloudSpeed);

        // After the step, so the frame is drawn at the moment asked for and not one step past it.
        if (const std::optional<Rtx::AirClock> held = mRun.getHeldAir())
        {
            mSky.holdAir(*held);
            mRenderer->holdAir(*held);
        }

        // Ahead of the trace, so the frame drawn is the one the window's extent asked for and not the one behind.
        mWindow.fit(*mRenderer, getCamera(), getPresentation(), getLoopClock().getNow());

        // The stop's upscaling, else the run's: beside the fit because both rebuild the targets, and the
        // camera below is built for the extent they leave.
        if (const Rtx::Upscale wanted = mRun.getUpscale().value_or(mUpscale);
            wanted != mRenderer->getProfile().mUpscale)
            mRenderer->setUpscale(wanted);
        const PendingSettings pending = std::exchange(mPending, PendingSettings{});
        if (pending.mVerticalSync.has_value())
            mRenderer->setVerticalSync(*pending.mVerticalSync);
        if (pending.mAnisotropy.has_value())
            mRenderer->setAnisotropy(*pending.mAnisotropy);
        if (pending.mGamma.has_value())
            mRenderer->setGamma(*pending.mGamma);
        if (pending.mIndirectLighting.has_value())
            mRenderer->setIndirectLighting(*pending.mIndirectLighting);
        if (pending.mBounceRays.has_value())
            mRenderer->setBounceRays(*pending.mBounceRays);

        // A frame under a cover is the interface alone: no walk, as the update traversal did not run, and no
        // trace, as the interface covers every pixel. The sweep goes with the walk, which marked nothing and
        // would otherwise take the world. `tws` is no cover: it is the world's view mask, read by the walk and
        // the trace. The emitter clock stops too, so a plume resumes where it left off. A picture of a subject
        // is drawn all the same, on a scene no walk of the world feeds.
        if (!isWorldShown())
        {
            mRipples.dropStrikes();
            mPhase.step(Phase::Views, Phase::Walking);
            mViews.draw(0, getFrameStamp(), mMirror.getRing(), mPictureReleases);
            mMirror.release(mPictureReleases);
            mPhase.step(Phase::Gui, Phase::Views);
            present();
            return;
        }

        // Where the eye stands, as the update traversal settled it on the camera this renderer
        // adopted: read here, at the one moment it is this frame's.
        const osg::Matrixd view = getCamera().getViewMatrix();

        // What disturbs the water this frame, decided before the walk and handed to the scene
        // beside the sprites, which is where the trace and the digest both read it.
        mRipples.update(frame.mWorld.mWater, !frame.mPaused);

        // The benchmark's `walk ms` row means the whole mirror: the walk and the sweep behind it.
        {
            const Crash::NoteScope walking("walking the scene");

            const std::chrono::steady_clock::time_point walked = std::chrono::steady_clock::now();
            mWalked.mFound = mMirror.mirror(frame, view);
            report.mSpend.at(Rtx::Timing::Walk) = Rtx::since(walked, std::chrono::steady_clock::now());
            report.mSpend.at(Rtx::Timing::Preprocess) = mWalked.mFound.mPreprocessed.mOnFrame.getMs();

            // The same graph again, which should add nothing; only a run that asked pays for the largest cost
            // a frame has.
            mWalked.mAgain.reset();
            if (mRun.wantsSecondWalk())
                mWalked.mAgain = mMirror.mirror(frame, view);

            mWalked.mSession += mWalked.mFound.mPreprocessed;
            if (mWalked.mAgain.has_value())
                mWalked.mSession += mWalked.mAgain->mPreprocessed;
            mWalked.mSession.mPrepared += mPrepared->takeStats();
            if (mContentStore != nullptr)
                mWalked.mStore = mContentStore->getStats();
        }

        // After the last walk, because a walk clears the frame's lists.
        mMirror.addRipples(mRipples.getImpulses());

        traceWorld(frame, view, report, since);

        // The one exit that stamps the frame as left, reached from a walk that placed nothing, a refused eye
        // or the run's hook after the trace.
        mPhase.step(Phase::Gui, Phase::Walking, Phase::Tracing, Phase::Run);
        present();
    }

    void RtxRenderer::traceWorld(
        const SceneFrame& frame, const osg::Matrixd& view, FrameReport& report, const std::optional<double> since)
    {
        if (mMirror.getScene().placements().getCounts().mPlaced == 0)
            return;

        // Described first, since a refused eye collects no frame: `RtxRun::frame` takes traced frames alone,
        // so the answer for the frame behind would reach no run. Left in the ring, the next traced frame
        // collects it.
        std::optional<Rtx::FrameRequest> request = describeTrace(frame, view);

        mPhase.step(Phase::Placing, Phase::Walking);
        {
            const Crash::NoteScope placing("placing the scene");
            if (request.has_value())
                finishBehind(report);

            // The frame behind is collected, so the tile copies it carried are there to paint: what
            // `TracedOverlay::paintTile` was asked before its picture had come back.
            mViews.finishOverlays();

            handOver(frame, report);
        }

        // After the scene and before the frame: a picture inside the interface traces against the copy of the
        // tables just handed over, and the frame's own trace stamps that copy as read. Above the eye check,
        // because such a picture brings its own eye.
        mPhase.step(Phase::Views, Phase::Placing);
        {
            const Crash::NoteScope pictures("drawing the pictures inside the interface");
            report.mSpend.at(Rtx::Timing::Views) = drawViews();
        }

        mPhase.step(Phase::Tracing, Phase::Views);
        const Crash::NoteScope tracing("tracing");
        if (!request.has_value())
        {
            mRenderer->skipFrame();
            return;
        }

        trace(frame, std::move(*request), report, since);
    }

    void RtxRenderer::finishBehind(FrameReport& report)
    {
        // Waited for ahead of the placement, which writes the copy of the tables the frame before last traced
        // and would otherwise absorb the stall into `place ms` as if it were placement work
        // (`RtxTool::FrameSamples`). `collectFrame` and not `finishFrame`: the frame behind stays on the
        // device while this one is placed, keeping it busy between traces (0.9 ms of a 6 ms frame on the ship);
        // the result describes the frame before it, so the bench row is two frames behind this one's wall time
        // (`Check::FramesOverlap` says the ring holds two). Timed as well, since the ring then reads counters
        // and destroys what that frame last read. The wait is this frame's whichever frame it waited for.
        const std::chrono::steady_clock::time_point finishing = std::chrono::steady_clock::now();
        report.mResult = mRenderer->collectFrame();
        report.mSpend.at(Rtx::Timing::Finish) = Rtx::since(finishing, std::chrono::steady_clock::now());
        report.mSpend.at(Rtx::Timing::Wait) = report.mResult.has_value() ? report.mResult->mWaitMs : 0.0;
    }

    void RtxRenderer::handOver(const SceneFrame& frame, FrameReport& report)
    {
        // Placed, appended or rebuilt: the decision is the harness's too, written once (`Rtx::SceneUploader`).
        const std::chrono::steady_clock::time_point handing = std::chrono::steady_clock::now();
        const Rtx::SceneUpload handed = mMirror.hand(*mRenderer, report.mSpend);
        report.mSpend.at(Rtx::Timing::Place) = Rtx::since(handing, std::chrono::steady_clock::now());
        report.mUpload = handed;
        report.mCellsToStand = mMirror.getRing().getCellsToStand();
        report.mImagesPending = static_cast<std::uint32_t>(mMirror.getImagesPending());

        if (handed.mKind == Rtx::SceneUpload::Kind::Rebuilt)
            Log(Debug::Info) << "Ray tracing built " << mMirror.getScene().meshes().getRows().size() << " meshes into "
                             << mWalked.mFound.mInstances << " instances with " << mWalked.mFound.mLights << " lights, "
                             << mWalked.mFound.mDeformed << " of them deforming, and skipped "
                             << mWalked.mFound.mSkippedUnknown << " it has no reader for";
    }

    void RtxRenderer::refuseEye(const std::string_view why)
    {
        // Once, because a camera nobody filled in and a real defect look identical from here until
        // it is said how often it happens.
        if (mComplained)
            return;

        mComplained = true;
        Log(Debug::Warning) << "Ray tracing skipped a frame: " << why;
    }

    std::optional<Rtx::FrameRequest> RtxRenderer::describeTrace(const SceneFrame& frame, const osg::Matrixd& view)
    {
        const Rtx::FrameExtents extents = mRenderer->getExtents();

        // The matrix and not a look-at: `getViewMatrixAsLookAt` hands back a point one unit ahead of the eye,
        // where a float ulp is a hundredth of a unit far out in Morrowind's cells, so differencing two such
        // points names a direction a fifth of a degree wide. A direction also carries no roll and borrows the
        // world's up, which has no answer for an eye looking straight up or down. A view matrix carries its own
        // basis. The frame's field of view and not the setting's: `WorldState` holds any override (zoom,
        // cutscene, script).
        std::optional<Rtx::Viewpoint> camera = Rtx::makeCameraFromView(view, frame.mEye.mFieldOfView,
            extents.mRenderWidth, extents.mRenderHeight, Rtx::sNearPlane, Rtx::sFarPlane);

        // Asked of the builder rather than tested for here, which would copy the builder's contract.
        if (!camera.has_value())
        {
            refuseEye(Rtx::isFieldOfView(frame.mEye.mFieldOfView) ? "the view matrix has no basis to look along"
                                                                  : "its field of view is outside 0 to 180 degrees");
            return std::nullopt;
        }

        // A script's projection offset: the same fraction of the picture at the traced extent as at
        // the frame's. The arms' eye is built after it and keeps it, as the rasterizer shifts the
        // arms' projection too.
        Rtx::shiftPicture(camera->mEyes.mWorld, frame.mEye.mProjectionShift);

        // The arms' own eye, at the field of view the game draws them through.
        const std::optional<Rtx::Shaders::Camera> arms
            = Rtx::cameraAtFieldOfView(camera->mEyes.mWorld, frame.mEye.mArmsFieldOfView);
        if (!arms.has_value())
        {
            refuseEye("the arms' field of view is outside 0 to 180 degrees");
            return std::nullopt;
        }
        camera->mEyes.mArms = *arms;

        // What the game decided the eye sees, read where the rasterizer reads it.
        const ViewDescription described = describeView(worldViewMask());

        // Walks the sampler and the jitter: left at zero the bounce samples the same point every frame and
        // the upscaler reconstructs from one sample taken repeatedly. The stop's own count where a run is
        // made, else the game's frame number (`RtxRun::getSampleFrame` says why).
        return Rtx::FrameRequest{
            .mView = *camera,
            .mRayMask = described.mRayMask,
            .mLamps = described.mLamps,
            .mSampleFrame = mRun.getSampleFrame().value_or(static_cast<std::uint32_t>(frame.mWhen.getFrameNumber())),
            .mWorld = {},
            .mSky = nullptr,
            .mOptions = {},
        };
    }

    void RtxRenderer::trace(
        const SceneFrame& frame, Rtx::FrameRequest request, FrameReport& report, const std::optional<double> since)
    {
        mSky.follow(
            frame.mSky, mMirror.getScene(), *getResources().getSceneManager(), mMirror.getWalkContext().mContent);
        request.mWorld = mSky.read(frame.mSky, frame.mWorld, frame.mPrecipitation, frame.mWhen.getSimulationTime(),
            frame.mEye.closesAirAt(mMirror.getReach()));
        request.mSky = &mSky.getContent();

        const double now = getLoopClock().getNow();
        const float sinceLast = mTracedAt.has_value() ? static_cast<float>(now - *mTracedAt) : 0.0f;
        mTracedAt = now;

        // The schedule's and not the profile's: a warm-up is not averaged in, which a half-built cell would
        // spoil (`RtxRun::getAccumulated`). The backend reads its own profile.
        request.mOptions = Rtx::FrameOptions{
            .mAccumulate = mRun.getAccumulated(),
            .mSinceLast = sinceLast,
            .mLoss = std::exchange(mLoss, Rtx::HistoryLoss::None),
            .mReconstruction = mRun.getReconstruction(),
            .mExposure = mRun.getExposure(),
            .mReadBack = mRun.wantsFrameCopy(),
        };

        // Timed because a profiler cannot read it: the record and submit are inside the driver, which carries
        // no frame pointer, so perf attributes them to an address with no caller.
        const std::chrono::steady_clock::time_point tracing = std::chrono::steady_clock::now();

        // What the debug modes drew, read off the world root here, after the game's own update
        // has rebuilt them for this frame and before the frame is recorded.
        if (mWorldRoot != nullptr)
            request.mOptions.mDebug = mDebugWalk.walk(*mWorldRoot, worldViewMask());

        report.mFrame = mRenderer->getFrameCount();
        const Rtx::FrameTraced traced = mRenderer->renderFrame(request);
        report.mConstants = traced.mConstants;
        report.mAir = Rtx::AirClock{ .mSky = mSky.getClock(), .mCarried = traced.mCarried };

        report.mSpend.at(Rtx::Timing::Trace) = Rtx::since(tracing, std::chrono::steady_clock::now());
        report.mSpend.at(Rtx::Timing::Present) = mTimer.takePresent();

        report.mWalked = mWalked;
        if (since.has_value())
            report.mSpend.at(Rtx::Timing::Frame) = *since;

        // Every traced frame, whether or not the device has answered for one yet, and the first
        // too, which a save or a skipped menu traces: the run counts the frames it traced, and
        // `RtxRun::frame` says why a count of answers is not that.
        mPhase.step(Phase::Run, Phase::Tracing);
        mRun.frame(describeContext(), report);

        // Once a second, which is how often `FrameTimer::addFrame` closes one — and the window is
        // asked then whether anybody can see it, rather than a copy of that being kept here.
        if (since.has_value() && mTimer.addFrame(*since))
            mWindow.setTitle(mTimer.writeTitle(mRun.describeTitle()).data());
    }
}
