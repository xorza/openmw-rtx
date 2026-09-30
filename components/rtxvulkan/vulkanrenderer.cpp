#include "vulkanrenderer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <ratio>
#include <span>
#include <string>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtx/environment/frameworld.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/framesampling.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtx/renderer/kernelprogress.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/present/presenter.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/placing.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracerecording.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace Rtx
{
    namespace
    {
        /// The instance a window needs, which is the headless one plus whatever SDL asks for — the
        /// surface among it, which is what tells the device to take a swapchain.
        std::vector<const char*> surfaceExtensionsFor(const RendererOptions& options)
        {
            if (options.mWindow == nullptr)
                return {};

            return Presenter::getInstanceExtensions(options.mWindow);
        }
    }

    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mInstance(options.mValidation, surfaceExtensionsFor(options))
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle()), options.mShaderDirectory,
              PipelineCacheSpec{ .mDirectory = options.mCacheDirectory })
        , mCounting(options.mCounting)
        , mProfile(options.mProfile)
        , mReadsCounts(mCounting || mProfile.mStressOverlapMs > 0.0)
        , mScenePasses(mDevice)
        , mTracePasses(mDevice, mScenePasses.mTextureLayout, mCounting, mProfile.mSpecializeLaunches)
        , mFrame(mDevice, mTracePasses)
        , mDisplay(mDevice, mTracePasses.mVisibility, mScenePasses.mTextureLayout.get(), PresentTargets::sFormat)
        , mDigest(mDevice)
        , mMedia(mDevice)
        , mGui(mDevice, PresentTargets::sFormat)
        , mPictures(mDevice, mTracePasses, mMedia, mDisplay, mGui.getTextures())
        , mUpscaler(mDevice)
    {
        mDevice.getMemory().limitBudget(options.mMemoryBudget);

        if (mProfile.mStressOverlapMs > 0.0)
            mStress = std::make_unique<StressPass>(mDevice, mProfile.mStressOverlapMs);

        // Before the first targets, because a windowed renderer is sized by its surface rather
        // than by what the caller guessed the window would come up at.
        if (options.mWindow != nullptr)
            mPresenter = std::make_unique<Presenter>(mDevice, mInstance, options.mWindow, options.mVerticalSync);

        const VkExtent2D output
            = mPresenter != nullptr ? mPresenter->getExtent() : VkExtent2D{ options.mWidth, options.mHeight };
        createTargets(output.width, output.height);
    }

    VulkanRenderer::~VulkanRenderer()
    {
        // What the interface handed over, before the pool holding it is taken apart. A GUI
        // texture write waits for nothing and rides the next submit this pool makes, and there is
        // no next submit here.
        tearDown("the interface's last writes were not submitted", [&] { mGui.getTextures().finish(); });

        // Every frame in flight, and the presenter's last blit, before the swapchain goes, which
        // is the one handle here not buried.
        tearDown("the device would not finish before the renderer was taken apart", [&] { mDevice.waitIdle(); });
    }

    void VulkanRenderer::resetHistory()
    {
        mFrame.resetHistory();
        mDisplay.resetHistory();
        mUpscaler.reset();
        mMedia.resetRipples();
    }

    void VulkanRenderer::drain()
    {
        mDevice.getPool().finishDeferred();
        mRing.finishAll();
        mDevice.waitIdle();

        // Everything buried, the scenes given back included, before what replaces them is made.
        mDevice.collectIdle();
    }

    void VulkanRenderer::setUpscale(Upscale upscale)
    {
        if (upscale == mProfile.mUpscale)
            return;

        mProfile.mUpscale = upscale;

        const VkExtent2D output = mTargets.getExtent();
        createTargets(output.width, output.height);
    }

    void VulkanRenderer::setSea(const SeaState& sea)
    {
        if (sea == mMedia.getSea())
            return;

        mMedia.describeSea(sea);
    }

    void VulkanRenderer::createTargets(std::uint32_t width, std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        // What to trace at is the mode's arithmetic, `extentsFor`, and the output where nothing
        // upscales.
        const VkExtent2D output{ width, height };
        const FrameExtents extents = extentsFor(width, height, mProfile.mUpscale);
        const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };
        mFrame.resize(render.width, render.height, mProfile.mRadianceWidth);

        // Two, and interchangeable, because the frame after this one must not rewrite the image
        // the present is still blitting out of. `PresentTargets` is what holds that rule.
        mTargets.resize(mDevice, width, height);

        // An upscaler a mode turned off keeps nothing but its pipelines: its images go with the mode.
        if (upscaling())
            mUpscaler.resize(render, output);
        else
            mUpscaler.release();

        // Over the output extent, which is what the frame is by the time the curve maps it: the
        // upscaler's output where one runs, and the trace itself, at the same size, where none does.
        mDisplay.resize(width, height);

        // A frame of a different size is not one this one can be reprojected against.
        mPreviousCamera = Shaders::VisibilityConstants{};
    }

    std::string VulkanRenderer::describeDevice() const
    {
        std::string report = "loader:            Vulkan " + versionString(mInstance.getApiVersion()) + '\n'
            + "validation:        " + (mInstance.getValidationLog() != nullptr ? "on" : "off") + '\n'
            + "debug utils:       " + (mInstance.hasDebugUtils() ? "on" : "off") + '\n';

        report += mDevice.getPhysicalDevice().describe();

        report += "\nupscaler:          " + std::string(Upscaler::describe()) + '\n';

        // Reaching here is the part that proves the rest: the device resolved every entry point the
        // required extensions promise, and a driver advertising one it cannot dispatch fails before
        // this line rather than at the first frame that needed it.
        report += "\nlogical device and every required entry point: ok\n";

        return report;
    }

    bool VulkanRenderer::isValidating() const
    {
        // The log exists only where the layer was found, so this answers "loaded" and not "asked".
        return mInstance.getValidationLog() != nullptr;
    }

    void VulkanRenderer::setScene(const SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> textures)
    {
        const Crash::NoteScope noted(
            "building the scene: {} meshes, {} textures", scene.meshes().getRows().size(), textures.size());

        // What the old scene held on the device buries itself, so a picture recorded against it and
        // not yet carried keeps it until the submit that carries the picture has run.
        mScenes.clear(slot);

        if (slot.isWorld())
        {
            // **A limit on memory, and not a matter of safety.** A new world is a load, and a load
            // may wait: the drain frees what the old one just buried, so a second world does not
            // hold two of everything at once — a cell's structures and textures are most of what
            // this renderer occupies.
            drain();

            // The reports of a world that has gone are dropped, and this is the only place they
            // are. A caller counts the frames it drew, so an arrival or a resize keeps its
            // reports and hands them over as it asks; a new world is that count starting again, and
            // a report from before it would answer the next question with the wrong frame.
            mRing.dropReports();

            // A sum over one scene means nothing over the next, so it goes back with the scene
            // rather than being carried empty into one it cannot describe. Neither does a motion
            // vector, which would point at where something stood in a world that is no longer there.
            mFrame.dropSum();
            mPreviousCamera = Shaders::VisibilityConstants{};

            // And the wake the old world's walkers left, which would ring on in the new one's
            // water wherever the two overlapped.
            mMedia.resetRipples();
        }

        // What the device has room for is decided below, against what it says now.
        mDevice.getMemory().refreshBudget(mDevice.getTimeline().getNext());

        Batch setup(mDevice.getPool());
        DeviceScene& held = mScenes.hold(
            slot, std::make_unique<DeviceScene>(mDevice, setup, mScenePasses, scene, textures, mProfile.mAnisotropy));

        // A picture's scene rides the next submit, as an arrival does: its placement and its trace
        // are deferred behind it, and the barrier every upload and build ends in orders them. The
        // world's is one submit for the whole cell, asked of the queue here — by hand rather than
        // left to the destructor, so a submit that fails throws out of here instead of being logged
        // on the way past.
        if (!slot.isWorld())
        {
            setup.defer();
            return;
        }

        setup.flush();
        held.readStats(mStats);
        mMedia.keepRipples(scene);
    }

    void VulkanRenderer::extendScene(const SceneSlot slot, const SceneDesc& scene, std::span<const TextureData> arrived)
    {
        const Crash::NoteScope noted("extending the scene: {} textures", arrived.size());

        DeviceScene& held = mScenes.at(slot);

        // An arrival does not wait for the frames in flight: what arrives is written on the queue,
        // behind whatever a frame in flight still reads of the room it was given, and the writes
        // end in the barrier `orderStagedWrites` records. It opens the frame it lands in, because
        // `beginFrame` clears the timer and a zone opened before it would be forgotten.
        GpuTimer* timer = nullptr;
        if (slot.isWorld())
            timer = &mRing.begin().mTimer;

        mDevice.getMemory().refreshBudget(mDevice.getTimeline().getNext());

        Batch setup(mDevice.getPool());
        held.extend(setup, scene, arrived, timer);

        // Deferred to the placement's submit: `placeScene` submits this ahead of the refit and the
        // top level, and the barrier every upload and build ends in orders them, so a composite
        // landing costs no submit, fence or wait of its own.
        setup.defer();

        // Always, because the top level names every instance and an arrival changed the list. It is
        // rebuilt every frame regardless, so an arrival costs it nothing.
        placeScene(slot, scene);

        // The history is kept. Nothing was renumbered, so what the last frame resolved still
        // describes the same surfaces — and throwing it away is a visible flash every time an actor
        // walks into view with a texture nobody has worn yet.
        if (slot.isWorld())
            held.readStats(mStats);
    }

    SceneHeld VulkanRenderer::describeHeld(const SceneSlot slot) const
    {
        const DeviceScene* held = mScenes.find(slot);
        return held != nullptr ? held->describe() : SceneHeld{};
    }

    std::span<const Refusal> VulkanRenderer::getRefusals(const SceneSlot slot) const
    {
        return mScenes.at(slot).getRefusals();
    }

    void VulkanRenderer::dropTextures(const SceneSlot slot, std::span<const Index> textures)
    {
        // Before there is a scene at all, which is one that swept before it was ever handed over.
        // There is nothing holding the images to destroy.
        if (DeviceScene* const held = mScenes.find(slot); held != nullptr)
            held->dropTextures(textures);
    }

    void VulkanRenderer::placeScene(const SceneSlot slot, const SceneDesc& scene)
    {
        DeviceScene& held = mScenes.at(slot);

        // The copy this placement writes is the one the last frame did not trace. The other copy
        // and not a parity of its own, because a frame need not place.
        const FrameSlot into = held.getSlot().next();

        // A picture of this copy recorded and carried by nothing yet is carried first, for what
        // `DeviceScene::pictureRides` says. Three placements of one scene inside one frame is the
        // only way here, which a game never takes.
        if (held.pictureRides(into, mDevice.getTimeline().getNext()))
            mDevice.getPool().finishDeferred();

        // Whatever last read or wrote this copy on the queue is waited for here, and each table
        // says what that was: the frame before last's trace, which `collectFrame` has usually
        // waited out already, so this is a comparison; an arrival's pose over the first copy,
        // carried by a placement's submit; a picture carried by the interface's own. Where the
        // device is behind, this is the right place for the CPU to stand still.
        held.finishReads(into);

        // A picture inside the interface is placed into a batch that rides the next submit, neither
        // timed nor opening the frame's report; the trace that follows is deferred the same way, and
        // the barrier `place` ends in orders the pair.
        if (!slot.isWorld())
        {
            // Nothing recorded is nothing carried, as for the world's below.
            Batch placement(mDevice.getPool());
            if (held.place(scene,
                    Placing{
                        .mCommands = placement.getCommands(),
                        .mSlot = into,
                    }))
                placement.defer();
            else
                placement.abandon();
            held.placed(into);
            return;
        }

        // A placement opens the frame, and every placement before a trace joins it. The frame's
        // report starts here and not at the trace: placing the world is the refit and the top level,
        // and a report that began at `renderFrame` would leave them out.
        FrameRecord& frame = mRing.begin();

        // The placement's own submit, without a wait. The frame's trace, later on the queue,
        // covers this submit too. Nothing recorded is nothing submitted, which is every frame of a
        // standing camera in an empty place.
        const VkCommandBuffer placement = mRing.takePlaceCommands(frame);
        mDevice.getPool().begin(placement);

        if (held.place(scene,
                Placing{
                    .mCommands = placement,
                    .mSlot = into,
                    .mTimer = &frame.mTimer,
                }))
            mDevice.getPool().submit(placement);
        else
            mDevice.getPool().end(placement);

        held.placed(into);

        held.readPlacedStats(mStats);
        mMedia.keepRipples(scene);
    }

    MemoryReport VulkanRenderer::getMemoryReport() const
    {
        return mDevice.getMemory().report();
    }

    void VulkanRenderer::setVerticalSync(SDLUtil::VSyncMode mode)
    {
        // Headless: `shot`, `bench` and `check` present to nothing, and a run with no surface has
        // no refresh to meet.
        if (mPresenter == nullptr)
            return;

        // A handed-over batch is submitted first, exactly as a resize does.
        mGui.getTextures().finish();
        mPresenter->setVerticalSync(mode);
    }

    void VulkanRenderer::setAnisotropy(const std::uint32_t anisotropy)
    {
        if (anisotropy == mProfile.mAnisotropy)
            return;

        mProfile.mAnisotropy = anisotropy;
        mScenes.forEach([&](DeviceScene& scene) { scene.setAnisotropy(anisotropy); });
    }

    void VulkanRenderer::skipFrame()
    {
        if (mRing.isOpen())
            mRing.skip();
    }

    std::uint64_t VulkanRenderer::getFrameCount() const
    {
        return mRing.getRecording();
    }

    std::optional<FrameResult> VulkanRenderer::finishFrame()
    {
        return mRing.collect();
    }

    std::optional<FrameResult> VulkanRenderer::collectFrame()
    {
        return mRing.collectFinished();
    }

    void VulkanRenderer::resize(std::uint32_t width, std::uint32_t height)
    {
        if (mPresenter != nullptr)
        {
            // Asked before anything is drained, because `RtxWindow::fit` calls this every settled
            // frame. Same reason as the destructor's: remaking a swapchain waits the device idle
            // and frees the blit's buffers, and a batch handed over is sitting beside them waiting
            // for a submit. What that costs where no rebuild follows is `Presenter::wantsResize`.
            if (mPresenter->wantsResize(VkExtent2D{ width, height }))
            {
                mGui.getTextures().finish();
                mPresenter->rebuild(VkExtent2D{ width, height });
            }

            // What the swapchain came back with, not what was asked for. A surface clamps to
            // what it can do, and targets sized to the request would then be blitted through a
            // scale nobody chose.
            const VkExtent2D shown = mPresenter->getExtent();
            width = shown.width;
            height = shown.height;
        }

        if (width == mTargets.getExtent().width && height == mTargets.getExtent().height)
            return;

        createTargets(width, height);
    }

    GuiSlot VulkanRenderer::addGuiTexture(std::uint32_t width, std::uint32_t height)
    {
        return mGui.getTextures().add(width, height);
    }

    std::span<std::uint8_t> VulkanRenderer::lendGuiTexture(const GuiSlot texture, const GuiRegion& region)
    {
        return mGui.getTextures().lend(texture, region);
    }

    void VulkanRenderer::sendGuiTexture(const GuiSlot texture)
    {
        mGui.getTextures().send(texture);
    }

    void VulkanRenderer::dropGuiTexture(const GuiSlot texture)
    {
        mGui.getTextures().drop(texture);
    }

    void VulkanRenderer::drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches)
    {
        assert(mTargets.isOpen());

        if (vertices.empty() || batches.empty())
            return;

        // After the frame's submit, and not waited for. The GUI is collected once the world has
        // been drawn and there is nothing to gain by holding the frame open for it; the queue draws
        // it after the frame, and the present blits after both.
        mGui.draw(vertices, batches, claimTarget());
    }

    void VulkanRenderer::presentFrame()
    {
        assert(mPresenter != nullptr && "presentFrame on a renderer that was given no window");
        assert(mTargets.isOpen());

        mPresenter->present(mTargets.current());
        mTargets.presented();
    }

    Image& VulkanRenderer::claimTarget()
    {
        // A present's blit outlives the call that queued it — under FIFO it waits until the
        // presentation engine has let that swapchain image go — and no barrier's source scope
        // reaches across a submit, so the target is waited for by the stamp the blit left on it.
        return mTargets.claim([](const Image& target) { target.waitIdle("the blit that last read this frame"); });
    }

    FrameExtents VulkanRenderer::getExtents() const
    {
        return FrameExtents{
            .mRenderWidth = mFrame.getWidth(),
            .mRenderHeight = mFrame.getHeight(),
            .mOutputWidth = mTargets.getExtent().width,
            .mOutputHeight = mTargets.getExtent().height,
        };
    }

    KernelProgress VulkanRenderer::awaitKernels(const std::chrono::milliseconds patience)
    {
        return mTracePasses.mVisibility.awaitKernels(patience);
    }

    Reconstruction VulkanRenderer::renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options)
    {
        mTracePasses.mVisibility.awaitKernels();

        const DeviceScene* const held = mScenes.find(SceneSlot::world());
        assert(held != nullptr && "renderFrame before setScene");
        const DeviceScene& world = *held;
        assert(camera.mCamera.mWidth == mFrame.getWidth() && camera.mCamera.mHeight == mFrame.getHeight()
            && "the camera has to be built for the render extent; ask getExtents");

        // Coverage and an upscaler do not meet: an upscaler writes the upscaled image itself and is
        // handed no coverage, so a picture that stops where nothing was hit is `traceGuiTexture`'s.
        assert((camera.mTransparentBackground == 0 || !upscaling())
            && "a frame that stops where nothing was hit belongs to traceGuiTexture, which does not upscale");

        // The frame `placeScene` opened, or a new one where nothing was placed.
        FrameRecord& frame = mRing.begin();

        const float sinceLastMs = options.mSinceLast * 1000.0f;

        // The miss count is an atomic sum over the frame, so the block starts each one at nothing
        // — and it is not started at all where nothing reads it back, which is the other half of
        // taking the counters out of the game: the atomics went with `COUNTING`, and this is the
        // write a frame that never reads it was still paying for.
        if (mReadsCounts)
            frame.mCounts.writable<Shaders::FrameCounts>(0, 1).front() = Shaders::FrameCounts{};

        // What reconstructs this frame, decided once and by one rule. Every switch below reads
        // this rather than working the interaction out again; the same value goes back in the frame
        // result, so what a run reports and what it did are one answer.
        const Reconstruction reconstruction = Reconstruction::resolve(
            mProfile.mUpscale, options.mReconstruction.value_or(mProfile.mReconstruction), getExtents());
        frame.mReconstruction = reconstruction;

        Shaders::VisibilityConstants sampled
            = sampleFrame(camera, options, mProfile, reconstruction, world.getCounts(), &mPreviousCamera);

        // The launch the misses are counted against, which is the traced extent and not the shown one.
        frame.mCountedRays = mCounting ? sampled.mCamera.mWidth * sampled.mCamera.mHeight : 0u;

        const TraceSubject subject
            = mMedia.describe(world, camera, frame.mCounts, mDisplay.getGlareCounts(), mRing.getRecordingSlot());

        // Every history is worthless after a jump no motion vector can describe: walking through a
        // door once left the previous camera intact and a reprojection fetched one room onto
        // another. What `resetHistory` said is each history's own, spent by the frame that reads
        // that history, so a reset before an unfiltered frame waits for the frame that filters.
        const bool basisLost = mPreviousCamera.mCamera.mForward.length2() <= 0.0f;
        if (basisLost)
            mUpscaler.reset();

        GpuTimer& timer = frame.mTimer;
        const VkCommandBuffer commands = frame.mWorld.mCommands;
        mDevice.getPool().begin(commands);

        // The glare fader's query starts the frame at nothing, ahead of the trace that counts.
        mDisplay.beginGlare(commands);

        // What walked through the water, stepped before the trace reads it and only where the
        // world stands in a sea: one field under every picture of this frame, anchored where the
        // step left it. A frame with no sea leaves the tiles as they were and stands no field.
        if (subject.mSea)
        {
            mMedia.stepRipples(commands, mRing.getRecordingSlot(), osg::Vec2f(camera.mOrigin.x(), camera.mOrigin.y()),
                joinSeconds(camera.mWaterTime), &timer);
            mMedia.placeRipples(sampled);
        }

        Image& target = claimTarget();

        const TraceResult traced = mFrame.record(commands,
            TraceRecording{
                .mSubject = subject,
                .mAsked = camera,
                .mSampled = sampled,
                .mReconstruction = reconstruction,
                .mAccumulate = options.mAccumulate,
                .mPastLost = basisLost,
                .mTimer = &timer,
            });
        const GBuffer& channels = traced.mInputs.mChannels;

        // Before anything past the trace has a say, and the channels' hand-over is the read this
        // rides on. `FrameDigest` says why the picture is not enough.
        if (options.mReadBack)
        {
            std::array<const Image*, Shaders::DIGEST_IMAGES> digested{};
            for (const Channel channel : sEveryChannel)
                digested[bindingOf(channel)] = &channels.get(channel);

            mDigest.record(commands, digested, frame.mDigestLanes, &timer);
            frame.mDigest = FrameDigest{
                .mJitterX = sampled.mCamera.mJitter.x(),
                .mJitterY = sampled.mCamera.mJitter.y(),
                .mFrameDeltaMs = sinceLastMs,
                .mReset = reconstruction.upscaled() && mUpscaler.isFresh() ? 1u : 0u,
            };
        }

        if (reconstruction.upscaled())
        {
            timer.open(commands, "upscale");
            mUpscaler.record(commands,
                UpscaleInputs{
                    .mColour = traced.mColour,
                    .mSurface = channels.get(Channel::Surface),
                    .mMotion = channels.get(Channel::Motion),
                    .mPuffs = channels.get(Channel::Puffs),
                    .mMasks = channels.get(Channel::UpscaleMasks),
                    .mCamera = sampled.mCamera,
                    .mArms = sampled.mArms,
                    .mJitterPhases = reconstruction.mJitterPhases,
                    .mSeconds = options.mSinceLast,
                    .mSlot = mRing.getRecordingSlot(),
                });
            timer.close(commands);
        }

        // The rest of the frame, over the reconstruction where something upscales and over the
        // trace's own composite where nothing does. The whole of the frame is the picture, which
        // is the output's extent either way.
        const bool upscaled = reconstruction.upscaled();

        FrameLook::Exposure exposure = FrameLook::Measured{
            .mSeconds = options.mSinceLast, .mReset = basisLost, .mBias = options.mExposureBias
        };
        const ExposureRule rule = options.mExposure.value_or(mProfile.mExposure);
        assert(!(rule.mFixed.has_value() && rule.mHeld) && "an exposure both fixed and held");
        if (rule.mFixed.has_value())
            exposure = FrameLook::Fixed{ *rule.mFixed };
        else if (rule.mHeld)
            exposure = FrameLook::Held{};

        mDisplay.record(commands,
            Display{
                .mTrace = traced,
                .mShown = upscaled ? mUpscaler.getOutput() : traced.mColour,
                .mShownFrom = upscaled ? Use::sAnyGeneralWrite : Use::sAnyGeneralRead,
                .mExtent = mTargets.getExtent(),
                .mSampled = sampled,
                .mTarget = target,
                .mFrame = FrameLook{
                    .mExposure = exposure,
                    .mGlare = FrameLook::Glare{ .mFader = options.mGlare, .mSeconds = options.mSinceLast, .mReset = basisLost },
                    .mDebug = options.mDebug,
                    .mDebugVertices = frame.mDebugVertices,
                    .mTimer = timer,
                },
            });

        // The picture as the curve left it and before the interface, into host memory of the
        // ring's, for the report that comes back with the frame. Grown here and not at the resize,
        // because most frames never ask.
        if (options.mReadBack)
        {
            const VkDeviceSize bytes = target.getReadBytes();
            GrowableBuffer& picture = mRing.pictureOf(mRing.getRecording());
            picture.growTo(bytes);
            target.recordRead(commands, Use::sComputeWrite, Use::sComputeWrite, picture.get());
            frame.mReadBackBytes = bytes;
        }

        // After the picture and inside the frame's trace, so the frame is finished when its value
        // has passed and the hold is the last thing it did.
        if (mStress != nullptr)
            mStress->record(commands, timer, frame.mCounts);

        // Submitted and not waited for: `finishFrame` or `collectFrame` brings the counts and the
        // report back a frame or two late. A wait's access scope is the device's, so the counts
        // need a dependency of their own, recorded here after every pass that could have written
        // them — the hold included.
        if (mReadsCounts)
            frame.mCounts.orderForHostRead(commands);

        mRing.submit(frame);

        // What the next frame reprojects against, and the camera as the caller gave it: a jitter is
        // where inside a pixel this frame sampled, not where the eye was.
        mPreviousCamera = camera;

        return reconstruction;
    }

    SceneSlot VulkanRenderer::addViewScene()
    {
        return mScenes.add();
    }

    void VulkanRenderer::dropViewScene(const SceneSlot scene)
    {
        // Not drained, because the scene's objects bury themselves: a drain here idled the whole
        // device every time the inventory closed.
        mScenes.drop(scene);
    }

    void VulkanRenderer::traceGuiTexture(
        const GuiSlot texture, const Shaders::VisibilityConstants& camera, const GuiTraceOptions& options)
    {
        mTracePasses.mVisibility.awaitKernels();

        assert(mGui.getTextures().holds(texture) && "a trace into a slot nothing holds");

        const VkExtent2D extent{ camera.mCamera.mWidth, camera.mCamera.mHeight };
        if (extent.width == 0 || extent.height == 0)
            return;

        if (!mPictures.holds(extent))
            mPictures.grow(extent, mProfile.mRadianceWidth);

        mPictures.trace(texture, camera, options, mScenes.at(options.mScene), mProfile);
    }

    bool VulkanRenderer::takeGuiCopy(const GuiSlot texture, const std::span<std::uint8_t> into)
    {
        return mGui.getTextures().takeCopy(texture, into);
    }

    void VulkanRenderer::finishGuiTraces()
    {
        // The pictures recorded and not yet carried, and then the frames carrying the rest; the
        // frame's own chain is left standing.
        mDevice.getPool().finishDeferred();
        mRing.finishAll();
    }

    void VulkanRenderer::readGuiTexture(const GuiSlot texture, std::vector<std::uint8_t>& pixels)
    {
        mGui.getTextures().read(texture, pixels);
    }

    void VulkanRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        assert(mTargets.isOpen());

        // The frame that was finished, not the one the next will be written into. A present has
        // already swapped those two; with no window nothing presents, nothing swaps, and the frame
        // just written is still the one `mTargets.current()` names.
        const Image& frame = mTargets.lastPresented() != nullptr ? *mTargets.lastPresented() : mTargets.current();
        frame.read(VK_IMAGE_LAYOUT_GENERAL, pixels);
    }

    void VulkanRenderer::readChannel(const Channel channel, std::vector<float>& values)
    {
        assert(mFrame.isBuilt());

        // One lookup and not a switch of eleven arms. A channel is its binding, and the buffer
        // is indexed by it.
        mFrame.getChannels().get(channel).readFloats(VK_IMAGE_LAYOUT_GENERAL, values);
    }

    void VulkanRenderer::readComposite(std::vector<float>& values)
    {
        assert(mFrame.isBuilt());
        mFrame.getColour().readFloats(VK_IMAGE_LAYOUT_GENERAL, values);
    }

    void VulkanRenderer::takeValidationErrors(std::vector<std::string>& errors)
    {
        errors.clear();

        ValidationLog* log = mInstance.getValidationLog();
        if (log == nullptr)
            return;

        log->takeErrorsOnThisThread(errors);
    }
}
