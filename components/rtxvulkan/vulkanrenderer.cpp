#include "vulkanrenderer.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crashnote.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/environment/wavespectrum.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/framepast.hpp>
#include <components/rtx/frame/framesampling.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/fogbuilder.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
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
#include <components/rtxvulkan/present/surface.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/placing.hpp>
#include <components/rtxvulkan/scene/sceneroom.hpp>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>
#include <components/rtxvulkan/trace/tracepast.hpp>
#include <components/rtxvulkan/trace/tracerecording.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>
#include <components/sdlutil/vsyncmode.hpp>

namespace Rtx
{
    namespace
    {
        /// What the frame's targets take under `profile` at an output `width` by `height`: what
        /// `createTargets` makes for it, and the running sum and the deep picture a frame may ask
        /// for where the run sums.
        VkDeviceSize frameAt(
            const Device& device, const std::uint32_t width, const std::uint32_t height, const RenderProfile& profile)
        {
            const VkExtent2D output{ width, height };
            const FrameExtents extents = extentsFor(width, height, profile.mUpscale);
            const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };
            VkDeviceSize bytes = PresentTarget::bytesAt(device, width, height)
                + DisplayChain::bytesAt(device, width, height)
                + TraceChain::bytesAt(device, render.width, render.height, profile.mRadianceWidth, TracePast::Kept);
            if (upscales(profile.mUpscale))
                bytes += Upscaler::bytesAt(device, render, output);

            // **Only a run at the reference's width sums**, which `RadianceWidth` argues: a player's
            // run is shown, and room for 8 bytes an output pixel and 16 a traced one would be room
            // its textures stood smaller for, 251 MB at 7680 by 2160 in the quality mode.
            if (profile.mRadianceWidth == RadianceWidth::Summed)
                bytes += TraceChain::sumBytesAt(device, render.width, render.height)
                    + PresentTarget::deepBytesAt(device, width, height);
            return bytes;
        }

        /// The instance a window needs, which is the headless one plus whatever SDL asks for — the
        /// surface among it, which is what tells the device to take a swapchain.
        std::vector<const char*> surfaceExtensionsFor(const RendererOptions& options)
        {
            if (options.mWindow == nullptr)
                return {};

            return Surface::getInstanceExtensions();
        }

        /// Whether a queue family presents to `surface`, or nothing to ask for no window.
        PhysicalDevice::PresentQuery presentsTo(const Surface* surface)
        {
            if (surface == nullptr)
                return {};

            return
                [surface](VkPhysicalDevice device, std::uint32_t family) { return surface->supports(device, family); };
        }
    }

    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mInstance(options.mRun.mValidation, surfaceExtensionsFor(options))
        , mSurface(options.mWindow != nullptr ? std::make_unique<Surface>(mInstance, options.mWindow) : nullptr)
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle(), presentsTo(mSurface.get())),
              options.mShaders, PipelineCacheSpec{ .mDirectory = options.mCacheDirectory })
        , mCounting(options.mCounting)
        , mProfile(options.mRun.mProfile)
        , mInverseGamma(1.0f / mProfile.mGamma)
        , mStress(mProfile.mStressOverlapMs > 0.0 ? std::make_unique<StressPass>(mDevice, mProfile.mStressOverlapMs)
                                                  : nullptr)
        , mRing(mDevice, mCounting || mStress != nullptr, options.mTiming,
              mStress != nullptr ? mStress->getTickMs() : 0.0)
        , mScenePasses(mDevice)
        , mTracePasses(mDevice, mScenePasses.mTextureLayout, mCounting, mProfile.mSpecializeLaunches)
        , mFrame(mDevice, mTracePasses, sFrameSlots, mProfile.mRadianceWidth, MemoryUse::Frame, TracePast::Kept)
        , mDisplay(mDevice, mTracePasses.mVisibility, mScenePasses.mTextureLayout)
        , mMedia(mDevice, FogNoise::shared())
        , mGui(mDevice)
        , mPictures(mDevice, mTracePasses, mMedia, mDisplay, mGui.getTextures(), mProfile.mRadianceWidth)
        , mUpscaler(mDevice)
    {
        mDevice.getMemory().limitBudget(options.mRun.mMemoryBudget);

        if (options.mWindow != nullptr)
            mPresenter = std::make_unique<Presenter>(mDevice, *mSurface, options.mWindow, options.mVerticalSync);

        createTargets(options.mWidth, options.mHeight);
    }

    VulkanRenderer::~VulkanRenderer()
    {
        // What the interface handed over, before the pool holding it is taken apart, and every
        // frame in flight and the presenter's last blit, before the swapchain goes, which is the
        // one handle here not buried. **A step apiece**, as `drain` takes them: a submit that
        // refused would otherwise skip the wait, and the swapchain would go under a frame still
        // on the queue.
        tearDown("the interface's last writes were not submitted", [&] { mGui.getTextures().finish(); });
        tearDown("the frames in flight were not finished", [&] { mRing.finishAll(); });
        tearDown("the device would not finish before the renderer was taken apart", [&] { mDevice.waitIdle(); });
    }

    void VulkanRenderer::drain()
    {
        mGui.getTextures().finish();
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

        const VkExtent2D output = mTarget.getExtent();
        createTargets(output.width, output.height);
    }

    void VulkanRenderer::setSea(const SeaState& sea)
    {
        if (sea == mMedia.getSea())
            return;

        mMedia.describeSea(sea);
    }

    void VulkanRenderer::setFogField(const FogNoise& noise)
    {
        drain();
        mMedia.describeFog(mDevice, noise);
    }

    void VulkanRenderer::createTargets(std::uint32_t width, std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        // What to trace at is the mode's arithmetic, `extentsFor`, and the output where nothing
        // upscales.
        const VkExtent2D output{ width, height };
        const FrameExtents extents = extentsFor(width, height, mProfile.mUpscale);
        const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };

        // A frame of a different size is not one this one can be reprojected against; a mode that
        // traces and shows at the sizes the last did keeps every history.
        const bool traceMoves = render.width != mFrame.getWidth() || render.height != mFrame.getHeight();
        const bool outputMoves = !mTarget.isOpen() || output.width != mTarget.getExtent().width
            || output.height != mTarget.getExtent().height;
        if (traceMoves || outputMoves)
            mPast |= FramePast::resized();

        // **Room for the running mode's targets, and not for the most any mode would take**: the
        // native mode's are about twice the quality mode's, which traces 2.25 times fewer pixels,
        // and room kept for them was room an 8 GB card's textures stood smaller for at a 4K output.
        const VkDeviceSize reserve = frameAt(mDevice, width, height, mProfile);

        // **The world is built again where the reserve moving changes what it would stand as**, by the
        // hand-over after this, which finds its slot empty (`SceneUploader`) and stands every texture
        // against the room the new targets leave, as a load does: a reserve that grew past what
        // content left is room the device does not have, and one that shrank is room content was
        // held back without. **Only there**, because a rebuild is every structure and every texture
        // in one flushed batch, seconds of a frame, and a reserve that moves within the room content
        // left makes the same world again.
        MemoryAllocator& memory = mDevice.getMemory();
        const VkDeviceSize reserved = memory.getFrameReserve();
        memory.refreshBudget(mDevice.getTimeline().getNext());
        memory.reserveFrame(reserve);
        const DeviceScene* const world = mScenes.find(SceneSlot::world());
        const bool rebuildsWorld = world != nullptr && reserve != reserved
            && (reserve > reserved ? !memory.contentFits() : world->wasHeldBack());

        // **The targets a mode does not keep go before the new are made**, so a change of mode
        // holds one set of them and never two: content stops where the set it takes still fits
        // (`MemoryUse::Frame`), which leaves no room for a second beside the first. A change of mode
        // may wait, as a new world does.
        const bool upscalerMoves = mUpscaler.isBuilt() && !(upscaling() && mUpscaler.isAt(render, output));
        const bool shownMoves = mTarget.isOpen() && outputMoves;
        if ((mFrame.isBuilt() && traceMoves) || shownMoves || upscalerMoves || rebuildsWorld)
        {
            drain();
            if (rebuildsWorld)
            {
                mReleasedWorld = world->describe().mIdentity;
                mScenes.clear(SceneSlot::world());
            }
            if (traceMoves)
                mFrame.release();
            if (shownMoves)
            {
                mTarget.release();
                mDisplay.release();
            }
            if (upscalerMoves)
                mUpscaler.release();
            mDevice.collectIdle();
        }

        mFrame.resize(render.width, render.height);

        mTarget.resize(mDevice, width, height);

        // An upscaler a mode turned off keeps nothing but its pipelines: its images went with the
        // mode, above.
        if (upscaling())
            mUpscaler.resize(render, output);

        // Over the output extent, which is what the frame is by the time the curve maps it: the
        // upscaler's output where one runs, and the trace itself, at the same size, where none does.
        mDisplay.resize(width, height);
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

        // The world a change of mode released, built again: one world, whose reports, sum, eye and
        // wake go on describing it, as across the change of mode itself. Its motion does not: what
        // moved since the last frame stands where the build found it, in both copies of its poses.
        const bool rebuilt = slot.isWorld() && std::exchange(mReleasedWorld, 0) == scene.getIdentity();
        if (rebuilt)
            mPast |= FramePast::resized();

        if (slot.isWorld() && !rebuilt)
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
            // rather than being carried empty into one it cannot describe. Neither does any history:
            // a motion vector would point at where something stood in a world that is no longer
            // there, and the old world's wake would ring on in the new one's water.
            mFrame.dropSum();
            mPast |= FramePast::everything();
        }

        // What the device has room for is decided below, against what it says now.
        mDevice.getMemory().refreshBudget(mDevice.getTimeline().getNext());

        Batch setup(mDevice.getPool());
        DeviceScene& held = mScenes.hold(slot,
            std::make_unique<DeviceScene>(mDevice, setup, mScenePasses, scene, textures, mProfile.mAnisotropy,
                slot.isWorld() ? sWorldRoom : SceneRoom{}));

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

        // The whole world's upload, every block of it free now that the flush has waited, and not
        // what any arrival after needs.
        mDevice.getPool().trimStaging(sStagingKept);

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

        // Deferred to the placement's submit: the `placeScene` the caller owes after this submits
        // it ahead of the refit and the top level, and the barrier every upload and build ends in
        // orders them, so a composite landing costs no submit, fence or wait of its own.
        setup.defer();

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
        Recording placement = mDevice.getPool().begin(mRing.takePlaceCommands(frame));

        if (held.place(scene,
                Placing{
                    .mCommands = placement.get(),
                    .mSlot = into,
                    .mTimer = &frame.mTimer,
                }))
            std::move(placement).submit();
        else
            std::move(placement).end();

        held.placed(into);

        held.readStats(mStats);
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

        // A handed-over batch is submitted first where a rebuild follows, exactly as a resize does,
        // and only there: most settings change no present mode, and the drain is a submit and a
        // wait. A present mode is a property of the swapchain object.
        mPresenter->setVerticalSync(mode, [this] { drain(); });
    }

    void VulkanRenderer::setAnisotropy(const std::uint32_t anisotropy)
    {
        if (anisotropy == mProfile.mAnisotropy)
            return;

        mProfile.mAnisotropy = anisotropy;
        mScenes.forEach([&](DeviceScene& scene) { scene.setAnisotropy(anisotropy); });
    }

    void VulkanRenderer::setGamma(const float gamma)
    {
        assert(gamma > 0.0f && std::isfinite(gamma) && "a gamma its reader should have refused");

        mProfile.mGamma = gamma;
        mInverseGamma = 1.0f / gamma;
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
        return mRing.finishFrame();
    }

    std::optional<FrameResult> VulkanRenderer::collectFrame()
    {
        return mRing.collectFrame();
    }

    void VulkanRenderer::resize(std::uint32_t width, std::uint32_t height)
    {
        if (width == mTarget.getExtent().width && height == mTarget.getExtent().height)
            return;

        createTargets(width, height);
    }

    void VulkanRenderer::showIn(std::uint32_t width, std::uint32_t height)
    {
        if (mPresenter == nullptr)
            return;

        // Asked before anything is drained, because `RtxWindow::fit` calls this every settled
        // frame. Same reason as the destructor's: remaking a swapchain waits the device idle and
        // frees the blit's buffers, and a batch handed over is sitting beside them waiting for a
        // submit. What that costs where no rebuild follows is `Presenter::wantsResize`.
        if (mPresenter->wantsResize(VkExtent2D{ width, height }))
        {
            drain();
            mPresenter->rebuild(VkExtent2D{ width, height });
        }
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
        assert(mTarget.isOpen());

        // After the frame's submit, and not waited for. The GUI is collected once the world has
        // been drawn and there is nothing to gain by holding the frame open for it; the queue draws
        // it after the frame, and the present blits after both. Drawn with no batches as well,
        // because what is shown is the picture under them either way.
        mGui.draw(vertices, batches, mTarget.getPicture(), mTarget.getShown());
        mTarget.showInterface();
    }

    void VulkanRenderer::presentFrame()
    {
        assert(mPresenter != nullptr && "presentFrame on a renderer that was given no window");
        assert(mTarget.isOpen());

        if (!mTarget.isShownCurrent())
            mGui.draw({}, {}, mTarget.getPicture(), mTarget.getShown());

        mPresenter->present(mTarget.getShown());
        mTarget.spendShown();
    }

    FrameExtents VulkanRenderer::getExtents() const
    {
        return FrameExtents{
            .mRenderWidth = mFrame.getWidth(),
            .mRenderHeight = mFrame.getHeight(),
            .mOutputWidth = mTarget.getExtent().width,
            .mOutputHeight = mTarget.getExtent().height,
        };
    }

    JobProgress VulkanRenderer::awaitKernels(const std::chrono::milliseconds patience)
    {
        return mTracePasses.mVisibility.awaitKernels(patience);
    }

    FrameTraced VulkanRenderer::renderFrame(const FrameRequest& request)
    {
        assert(request.mSky != nullptr && "a frame described with no sky to read its sheets from");

        FrameTraced traced{ .mConstants = constantsFor(request.mView), .mCarried = {} };
        Shaders::VisibilityConstants& constants = traced.mConstants;
        constants.mRayMask = request.mRayMask;
        constants.mNoLamps = request.mLamps ? 0u : 1u;
        constants.mFrame = request.mSampleFrame;

        WorldOptions described;
        describeWorld(request.mWorld, *request.mSky, mDrift, constants, described);
        traced.mCarried = mDrift.get();

        renderFrame(constants, request.mOptions, described);
        return traced;
    }

    void VulkanRenderer::holdAir(const AirClock& air)
    {
        mDrift.hold(air.mCarried, air.mSky.mSeconds);
    }

    void VulkanRenderer::renderFrame(
        const Shaders::VisibilityConstants& camera, const FrameOptions& options, const WorldOptions& described)
    {
        const DeviceScene* const held = mScenes.find(SceneSlot::world());
        assert(held != nullptr && "renderFrame before setScene");
        const DeviceScene& world = *held;
        assert((options.mAccumulate == 0 || mProfile.mRadianceWidth == RadianceWidth::Summed)
            && "a sum of frames in a run that only shows them, whose reserve keeps no room for it");
        assert(camera.mEyes.mWorld.mWidth == mFrame.getWidth() && camera.mEyes.mWorld.mHeight == mFrame.getHeight()
            && "the camera has to be built for the render extent; ask getExtents");

        // Coverage and an upscaler do not meet: an upscaler writes the upscaled image itself and is
        // handed no coverage, so a picture that stops where nothing was hit is `traceGuiTexture`'s.
        assert((camera.mTransparentBackground == 0 || !upscaling())
            && "a frame that stops where nothing was hit belongs to traceGuiTexture, which does not upscale");

        // The frame `placeScene` opened, or a new one where nothing was placed.
        FrameRecord& frame = mRing.begin();

        const float sinceLastMs = options.mSinceLast * 1000.0f;

        // What reconstructs this frame, decided once and by one rule. Every switch below reads
        // this rather than working the interaction out again; the same value goes back in the frame
        // result, so what a run reports and what it did are one answer.
        const Reconstruction reconstruction = Reconstruction::resolve(
            mProfile.mUpscale, options.mReconstruction.value_or(mProfile.mReconstruction), getExtents());
        frame.mReconstruction = reconstruction;

        // Every history is worthless after a jump no motion vector can describe: through a door,
        // with the previous camera kept, a reprojection would fetch one room onto another. Spent
        // here, by the frame it describes, whatever made it.
        FramePast past = std::exchange(mPast, FramePast{});
        past |= FramePast::of(options.mLoss);
        assert((past.mReprojectionLost || mPreviousCamera.has_value()) && "a past with no camera to reproject");

        Shaders::VisibilityConstants sampled = sampleFrame(camera, options, mProfile, reconstruction, world.getCounts(),
            past.mReprojectionLost ? nullptr : &*mPreviousCamera);
        world.measureStars(sampled.mStars);

        // The launch the misses are counted against, which is the traced extent and not the shown one.
        frame.mCountedRays = mCounting ? sampled.mEyes.mWorld.mWidth * sampled.mEyes.mWorld.mHeight : 0u;

        const TraceSubject subject
            = mMedia.describe(world, camera, frame.mCounts, mDisplay.getGlareCounts(), mRing.getRecordingSlot());

        if (past.mReprojectionLost)
            mUpscaler.reset();
        if (past.mEyeLost)
            mDisplay.loseEye();
        if (past.mWaterLost)
            mMedia.resetRipples();

        GpuTimer& timer = frame.mTimer;
        Recording trace = mRing.recordWorld(frame);
        const VkCommandBuffer commands = trace.get();

        // The glare fader's query starts the frame at nothing, ahead of the trace that counts.
        mDisplay.beginGlare(commands);

        // What walked through the water, stepped before the trace reads it and only where the
        // world stands in a sea: one field under every picture of this frame, anchored where the
        // step left it. A frame with no sea leaves the tiles as they were and stands no field.
        if (subject.mSea)
        {
            mMedia.stepRipples(commands, mRing.getRecordingSlot(), osg::Vec2f(camera.mOrigin.x(), camera.mOrigin.y()),
                described.mWaterSeconds, &timer);
            mMedia.placeRipples(sampled);
        }

        Image* const deep = mTarget.beginPicture(mDevice, options.mAccumulate > 0);
        Image& target = mTarget.getPicture();

        const TraceResult traced = mFrame.record(commands,
            TraceRecording{
                .mSubject = subject,
                .mAsked = BinCamera::of(camera),
                .mSampled = sampled,
                .mReconstruction = reconstruction,
                .mAccumulate = options.mAccumulate,
                .mPastLost = past.mReprojectionLost,
                .mTimer = &timer,
            });
        const GBuffer& channels = traced.mInputs.mChannels;

        // Before anything past the trace has a say, and the channels' hand-over is the read this
        // rides on. `FrameDigest` says why the picture is not enough.
        if (options.mReadBack)
        {
            std::array<const Image*, Shaders::DIGEST_IMAGES> digested{};
            for (const Channel channel : sEveryChannel)
                digested[indexOf(channel)] = &channels.get(channel);

            mRing.readDigest(frame, commands, digested,
                FrameDigest{
                    .mJitterX = sampled.mEyes.mWorld.mJitter.x(),
                    .mJitterY = sampled.mEyes.mWorld.mJitter.y(),
                    .mFrameDeltaMs = sinceLastMs,
                    .mReset = reconstruction.upscaled() && mUpscaler.isFresh() ? 1u : 0u,
                },
                &timer);
        }

        // The rest of the frame, over the reconstruction where something upscales and over the
        // trace's own composite where nothing does. The whole of the frame is the picture, which
        // is the output's extent either way.
        const HandedImage shown = [&] {
            if (!reconstruction.upscaled())
                return traced.mColour;

            return mUpscaler.record(commands,
                UpscaleInputs{
                    .mColour = traced.mColour.mImage,
                    .mSurface = channels.get(Channel::Surface),
                    .mMotion = channels.get(Channel::Motion),
                    .mMasks = channels.get(Channel::UpscaleMasks),
                    .mEyes = sampled.mEyes,
                    .mPreviousJitter = sampled.mPreviousJitter,
                    .mJitterPhases = reconstruction.mJitterPhases,
                    .mSeconds = options.mSinceLast,
                    .mSlot = mRing.getRecordingSlot(),
                },
                &timer);
        }();

        mDisplay.record(commands,
            Display{
                .mTrace = traced,
                .mShown = shown,
                .mUpscaled = reconstruction.upscaled(),
                .mExtent = mTarget.getExtent(),
                .mSampled = sampled,
                .mTarget = target,
                .mLeftAs = PresentTarget::sResting,
                .mFrame = FrameLook{
                    .mExposure = options.mExposure.value_or(mProfile.mExposure),
                    .mExposureBias = described.mExposureBias,
                    .mSeconds = options.mSinceLast,
                    .mGlare = described.mGlare,
                    .mInverseGamma = mInverseGamma,
                    .mNightEye = described.mNightEye,
                    .mDither = options.mDither.value_or(mProfile.mDither),
                    .mDeep = deep,
                    .mDebug = options.mDebug,
                    .mDebugVertices = frame.mDebugVertices,
                    .mTimer = timer,
                },
            });

        // The picture as the curve left it and before the interface.
        if (options.mReadBack)
            mRing.readPicture(frame, commands, target);

        // After the picture and inside the frame's trace, so the frame is finished when its value
        // has passed and the hold is the last thing it did.
        if (mStress != nullptr)
            mStress->record(commands, frame.mCounts, &timer);

        // Submitted and not waited for: `finishFrame` or `collectFrame` brings the counts and the
        // report back a frame or two late.
        mRing.submit(frame, std::move(trace));

        // What the next frame reprojects against, and the camera as the caller gave it: a jitter is
        // where inside a pixel this frame sampled, not where the eye was.
        mPreviousCamera = camera;
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

    void VulkanRenderer::traceGuiTexture(const GuiSlot texture, const Viewpoint& view, const GuiTraceOptions& options)
    {
        Shaders::VisibilityConstants camera = constantsFor(view);
        camera.mSun = Shaders::sunSource(options.mLight.mDirection, options.mLight.mIrradiance);
        camera.mAmbient = options.mLight.mAmbient;
        camera.mNoSkyShadows = 1;
        camera.mTransparentBackground = options.mClear[3] < 1.0f ? 1u : 0u;
        camera.mRayMask = options.mRayMask;
        camera.mNoLamps = options.mLamps ? 0u : 1u;

        traceGuiTexture(texture, camera, options);
    }

    void VulkanRenderer::traceGuiTexture(
        const GuiSlot texture, const Shaders::VisibilityConstants& camera, const GuiTraceOptions& options)
    {
        assert(mGui.getTextures().holds(texture) && "a trace into a slot nothing holds");

        const VkExtent2D extent{ camera.mEyes.mWorld.mWidth, camera.mEyes.mWorld.mHeight };
        if (extent.width == 0 || extent.height == 0)
            return;

        if (!mPictures.holds(extent))
            mPictures.grow(extent);

        mPictures.trace(texture, camera, options, mScenes.at(options.mScene), mProfile);
    }

    bool VulkanRenderer::takeGuiCopy(const GuiSlot texture, const std::span<std::uint8_t> into)
    {
        return mGui.getTextures().takeCopy(texture, into);
    }

    void VulkanRenderer::finishGuiTraces()
    {
        drain();
    }

    void VulkanRenderer::readGuiTexture(const GuiSlot texture, std::vector<std::uint8_t>& pixels)
    {
        mGui.getTextures().read(texture, pixels);
    }

    void VulkanRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        assert(mTarget.isOpen());

        mTarget.getPicture().read(VK_IMAGE_LAYOUT_GENERAL, pixels);
    }

    void VulkanRenderer::readDeepPixels(std::vector<std::uint16_t>& samples)
    {
        std::vector<std::uint8_t> bytes;
        mTarget.getDeep().read(VK_IMAGE_LAYOUT_GENERAL, bytes);
        samples.resize(bytes.size() / sizeof(std::uint16_t));
        std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(std::uint16_t));
    }

    void VulkanRenderer::readShown(std::vector<std::uint8_t>& pixels)
    {
        assert(mTarget.isOpen());

        mTarget.getShown().read(VK_IMAGE_LAYOUT_GENERAL, pixels);
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
