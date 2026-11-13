#include "vulkanrenderer.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <osg/Vec2f>
#include <vulkan/vulkan_core.h>

#include <components/crashcatcher/crash.hpp>
#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/dlss/dlss.hpp>
#include <components/platform/memory.hpp>
#include <components/rtx/common/index.hpp>
#include <components/rtx/common/jobprogress.hpp>
#include <components/rtx/frame/frameoptions.hpp>
#include <components/rtx/frame/framepast.hpp>
#include <components/rtx/frame/framesampling.hpp>
#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/denoiserimage.hpp>
#include <components/rtx/renderer/framedigest.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/renderer/memoryreport.hpp>
#include <components/rtx/renderer/slot.hpp>
#include <components/rtx/renderer/verticalsync.hpp>
#include <components/rtx/scene/scenedesc.hpp>
#include <components/rtx/shaders/digest.h>
#include <components/rtx/shaders/gbuffer.h>
#include <components/rtx/shaders/visibility.h>
#include <components/rtx/world/fognoise.hpp>
#include <components/rtx/world/frameworld.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/memory/memory.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>
#include <components/rtxvulkan/device/physicaldevice.hpp>
#include <components/rtxvulkan/device/pipelinecache.hpp>
#include <components/rtxvulkan/device/requirements.hpp>
#include <components/rtxvulkan/device/result.hpp>
#include <components/rtxvulkan/device/timeline.hpp>
#include <components/rtxvulkan/device/validation.hpp>
#include <components/rtxvulkan/frame/digestimages.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/frame/tracepast.hpp>
#include <components/rtxvulkan/present/presenter.hpp>
#include <components/rtxvulkan/present/surface.hpp>
#include <components/rtxvulkan/scene/devicescene.hpp>
#include <components/rtxvulkan/scene/placing.hpp>
#include <components/rtxvulkan/shaders/shared/fields.h>
#include <components/rtxvulkan/texture/texture.hpp>
#include <components/rtxvulkan/trace/tracerecording.hpp>
#include <components/rtxvulkan/trace/visibilitypass.hpp>
#include <components/rtxvulkan/upscale/dlssapplication.hpp>
#include <components/rtxvulkan/upscale/upscaler.hpp>

namespace Rtx
{
    namespace
    {
        /// What the frame's targets take under `profile` at an output `width` by `height` with the
        /// pictures' chain at `pictures`: what `createTargets` makes, including the arena of `declared`
        /// and, where the run sums, the running sum and the deep picture.
        VkDeviceSize frameAt(const Device& device, const std::uint32_t width, const std::uint32_t height,
            const VkExtent2D pictures, const RenderProfile& profile, const Upscaler& upscaler,
            const std::span<const ArenaImage> declared)
        {
            const VkExtent2D output{ width, height };
            const FrameExtents extents = upscaler.extentsFor(width, height, profile.mUpscale);
            const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };
            VkDeviceSize bytes = PresentTarget::bytesAt(device, width, height)
                + DisplayChain::bytesAt(device, width, height)
                + FrameTracer::bytesAt(
                    device, render, output, profile.mUpscale, upscaler, profile.mRadianceWidth, declared);
            if (pictures.width > 0)
                bytes += PictureTracer::bytesAt(device, pictures, profile.mRadianceWidth, upscaler.getMasks());

            // **Only a run at the reference's width sums** (`RadianceWidth`): room for the sum is room a
            // player's textures would stand smaller for, 251 MB at 7680 by 2160 in the quality mode.
            if (profile.mRadianceWidth == RadianceWidth::Summed)
                bytes += TraceChain::sumBytesAt(device, render.width, render.height)
                    + PresentTarget::deepBytesAt(device, width, height);
            return bytes;
        }

        /// The headless instance's extensions plus whatever SDL asks for, the surface's among them, which
        /// is what tells the device to take a swapchain.
        std::vector<const char*> surfaceExtensionsFor(const RendererOptions& options)
        {
            if (options.mWindow == nullptr)
                return {};

            return Surface::getInstanceExtensions();
        }

        /// What NGX needs on the instance, where the run may use DLSS.
        std::vector<std::string> dlssInstanceExtensionsFor(const std::optional<Dlss::Application>& application)
        {
            return application.has_value() ? application->requiredInstanceExtensions() : std::vector<std::string>();
        }

        /// Whether the frames report to the host (`RendererOptions::mReporting`).
        bool measured(const RendererOptions& options)
        {
            return options.mReporting == Reporting::Measured;
        }

        /// What NGX is told of the application, where the run may use DLSS: a profile that only
        /// ever uses FSR makes none, and no folder is made for it.
        std::optional<Dlss::Application> dlssApplicationFor(const RendererOptions& options)
        {
            if (options.mRun.mProfile.mUpscaler == UpscalerChoice::Fsr)
                return std::nullopt;

            return std::optional<Dlss::Application>(std::in_place, options.mDlss);
        }

        /// What NGX asks of a device for DLSS, where the run may use DLSS.
        std::function<std::vector<std::string>(VkInstance, VkPhysicalDevice, const DriverProfile&)> dlssExtensionsFor(
            const std::optional<Dlss::Application>& application)
        {
            if (!application.has_value())
                return nullptr;

            const Dlss::Application& asked = *application;
            return [&asked](const VkInstance instance, const VkPhysicalDevice device, const DriverProfile& driver) {
                return asked.requiredDeviceExtensions(instance, device, driver);
            };
        }

        /// Whether a queue family presents to `surface`, or nothing to ask for no window.
        PhysicalDevice::PresentQuery presentsTo(const Surface* surface)
        {
            if (surface == nullptr)
                return {};

            return [surface](VkPhysicalDevice device, std::uint32_t family) {
                // A surface lost while the device is chosen presents nothing yet.
                return surface->supports(device, family).value_or(false);
            };
        }
    }

    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mDlss(dlssApplicationFor(options))
        , mInstance(options.mRun.mValidation, surfaceExtensionsFor(options), dlssInstanceExtensionsFor(mDlss))
        , mSurface(options.mWindow != nullptr ? std::make_unique<Surface>(mInstance, options.mWindow) : nullptr)
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle(), presentsTo(mSurface.get())),
              options.mShaders, PipelineCacheSpec{ .mDirectory = options.mCacheDirectory },
              DeviceReport{ .mCodeFolder = options.mRun.mPipelineCode,
                  .mCounters = options.mRun.mZoneCounters,
                  .mMarkers = options.mRun.mValidation.mMarkers,
                  .mCheckpoints = options.mRun.mValidation.mCheckpoints,
                  .mAskedExtensions = dlssExtensionsFor(mDlss) })
        , mUpscaler(mDevice, options.mRun.mProfile.mUpscaler,
              DlssUpscaler::availabilityOn(mDevice.getPhysicalDevice().getDriver(), options.mDlss),
              mDlss.has_value() ? &*mDlss : nullptr)
        , mUpscalers(offeredUpscalers(offersDlss(mUpscaler.getDlss())))
        , mCounting(measured(options))
        , mProfile(options.mRun.mProfile)
        , mInverseGamma(1.0f / mProfile.mGamma)
        , mStress(mProfile.mStressOverlapMs > 0.0 ? std::make_unique<StressPass>(mDevice, mProfile.mStressOverlapMs)
                                                  : nullptr)
        , mRing(mDevice, options.mReporting)
        , mReadBack(mDevice, BufferKind::ReadBack, VK_BUFFER_USAGE_TRANSFER_DST_BIT, "picture read back")
        , mScenePasses(mDevice)
        , mArrival(mDevice)
        , mTracePasses(
              mDevice, mScenePasses.mTextureLayout, mCounting, mProfile.mSpecializeKernels, mUpscaler.getMasks())
        , mFrame(mDevice, mTracePasses, mProfile.mRadianceWidth)
        , mDisplay(mDevice, mTracePasses.mVisibility, mScenePasses.mTextureLayout)
        , mMedia(mDevice, sFogNoise)
        , mGui(mDevice)
        , mPictures(mDevice, mTracePasses, mProfile.mRadianceWidth)
        , mReservedUpscales(options.mRun.mReservedUpscales)
    {
        mDevice.getMemory().limitBudget(options.mRun.mMemoryBudget);
        mProfile.mUpscaler = mUpscaler.getChoice();

        // **NGX's hazards between its own commands on its own resources are not the renderer's to answer
        // for** (seen in Ray Reconstruction 310.9.16: a clear after its own layout transition).
        if (ValidationLog* const log = mInstance.getValidationLog())
            if (const std::optional<std::string_view> runtime = mUpscaler.getRuntimeResources())
                log->disown(std::string(*runtime));

        if (options.mWindow != nullptr)
            mPresenter
                = std::make_unique<Presenter>(mDevice, std::move(mSurface), options.mWindow, options.mVerticalSync);

        createTargets(options.mWidth, options.mHeight);
    }

    VulkanRenderer::~VulkanRenderer()
    {
        // The interface's writes go before the pool holding them is taken apart, and every frame in flight
        // and the presenter's last blit before the swapchain, the one handle here not buried. **A step
        // apiece**: a refused submit would otherwise skip the wait and the swapchain would go under a frame.
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

    void VulkanRenderer::createTargets(std::uint32_t width, std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        // What to trace at is the upscaler's answer for the mode, and the output where nothing
        // upscales.
        const VkExtent2D output{ width, height };
        const FrameExtents extents = mUpscaler.extentsFor(width, height, mProfile.mUpscale);
        const VkExtent2D render{ extents.mRenderWidth, extents.mRenderHeight };
        const Crash::NoteScope noted("making the frame's targets: {}x{} shown, traced at {}x{}, upscale {}",
            output.width, output.height, render.width, render.height, sUpscaleNames.name(mProfile.mUpscale));

        std::vector<ArenaImage> declared;
        FrameTracer::declare(render, output, mProfile, mUpscaler, declared);
        const FrameTracer::Moves moves = mFrame.movesTo(render, output, mProfile.mUpscale, declared, mUpscaler);

        // A frame of a different size is not one this one can be reprojected against; a mode that
        // traces and shows at the sizes the last did keeps every history.
        if (moves.mRenderExtent || moves.mOutputExtent)
            mDescriber.lose(FramePast::resized());

        // **Logged where the frame differs from the last one logged or anything is made again**: the mode
        // alone, Native against Off, moves no extent, and a mode that keeps its extents keeps the chain and
        // every history; a resize to the size the targets stand at says nothing new.
        std::string frame
            = std::format("{}x{} shown, traced at {}x{} ({}, {:.1f}% a side), by {}, the light filtered by the {}",
                output.width, output.height, render.width, render.height, sUpscaleNames.name(mProfile.mUpscale),
                100.0 * render.width / output.width, sUpscalerNames.name(mUpscaler.getChoice()),
                rendererFilters(mProfile.mUpscale, mUpscaler.getInput()) ? "renderer" : "upscaler");
        if (frame != mFrameLogged || moves.mTrace || moves.mUpscaler)
            Log(Debug::Info) << "Frame: " << frame << ", the trace chain " << (moves.mTrace ? "made again" : "kept")
                             << ", the upscaler's targets " << (moves.mUpscaler ? "made again" : "kept");
        // What a report of a crash after this says the frame was.
        Crash::annotate("frame", frame);
        mFrameLogged = std::move(frame);

        // **The targets a mode does not keep go before the new are made**, so a change of mode holds one
        // set and never two: content stops where one set fits (`MemoryUse::Frame`), leaving no room for a
        // second.
        const bool shownMoves = mTarget.isOpen() && moves.mOutputExtent;
        const bool picturesMove = mPictures.isBuilt() && !mPictures.isAt(mPictureRoom);
        if (mFrame.releases(moves) || shownMoves || picturesMove)
        {
            drain();
            mFrame.release(moves, mUpscaler);
            if (shownMoves)
            {
                mTarget.release();
                mDisplay.release();
            }
            if (picturesMove)
                mPictures.release();
            if (moves.mRenderExtent || moves.mArena)
                mShownRadiance = nullptr;
            mDevice.collectIdle();
        }

        // **What the upscaler's runtime allocates is made before the reserve that counts it**: DLSS's
        // feature is hundreds of megabytes the driver shows only once it stands. Each mode the run moves
        // to is measured once at this output (`Upscaler::measure`).
        if (upscales(mProfile.mUpscale))
            mUpscaler.prepare(mProfile.mUpscale, render, output);
        for (const Upscale mode : mReservedUpscales)
            if (upscales(mode) && mode != mProfile.mUpscale)
            {
                const FrameExtents its = mUpscaler.extentsFor(width, height, mode);
                mUpscaler.measure(mode, VkExtent2D{ its.mRenderWidth, its.mRenderHeight }, output);
            }
        MemoryAllocator& memory = mDevice.getMemory();
        memory.holdOutside(mUpscaler.getHeldOutside());

        // **Room for the running mode's targets, not for the most any mode would take**: native's are about
        // twice the quality mode's, room an 8 GB card's textures stood smaller for at a 4K output. A run
        // that names its modes reserves for the most of them (`RunProfile::mReservedUpscales`).
        VkDeviceSize reserve = frameAt(mDevice, width, height, mPictureRoom, mProfile, mUpscaler, declared);
        for (const Upscale mode : mReservedUpscales)
        {
            RenderProfile reserved = mProfile;
            reserved.mUpscale = mode;
            const FrameExtents its = mUpscaler.extentsFor(width, height, mode);
            std::vector<ArenaImage> itsDeclared;
            FrameTracer::declare(
                VkExtent2D{ its.mRenderWidth, its.mRenderHeight }, output, reserved, mUpscaler, itsDeclared);
            reserve
                = std::max(reserve, frameAt(mDevice, width, height, mPictureRoom, reserved, mUpscaler, itsDeclared));
        }

        // **The world is built again only where the reserve moving changes what it would stand as**: the
        // hand-over after this finds its slot empty (`SceneUploader`) and stands every texture against the
        // new room. A rebuild is every structure and texture in one flushed batch, seconds of a frame.
        const VkDeviceSize reserved = memory.getFrameReserve();
        memory.refreshBudget(mDevice.getTimeline().getNext());
        memory.reserveFrame(reserve);
        const DeviceScene* const world = mScenes.find(SceneSlot::world());
        const bool rebuildsWorld = world != nullptr && reserve != reserved
            && (reserve > reserved ? !memory.contentFits() : world->wasHeldBack());
        if (rebuildsWorld)
        {
            drain();
            mReleasedWorld = world->describe().mIdentity;
            mScenes.clear(SceneSlot::world());
            mDevice.collectIdle();
        }

        // **Placed again only where the arena moved**, which the device was drained for.
        mFrame.resize(moves, render, output, mProfile.mUpscale, std::move(declared), mUpscaler);

        mTarget.resize(mDevice, width, height);

        // Over the output extent: the upscaler's output where one runs, the trace itself where none does.
        mDisplay.resize(width, height);

        if (mPictureRoom.width > 0)
            mPictures.resize(mPictureRoom);
    }

    CardAddress VulkanRenderer::getCardAddress() const
    {
        return mDevice.getPhysicalDevice().getAddress();
    }

    std::string VulkanRenderer::describeDevice() const
    {
        std::string report = "loader:            Vulkan " + versionString(mInstance.getApiVersion()) + '\n'
            + "validation:        " + (mInstance.getValidationLog() != nullptr ? "on" : "off") + '\n'
            + "debug utils:       " + (mInstance.hasDebugUtils() ? "on" : "off") + '\n';

        report += mDevice.getPhysicalDevice().describe();
        report += mInstance.getLayers().describe();

        report += "\nupscaler:          " + mUpscaler.describeChoice() + '\n';

        // What the video memory comes to once the frame's targets stand, and what the system leaves the
        // process beside it: the two figures a report of running short is read against.
        report += "\nmemory\n" + describeMemory(mDevice.getMemory().report());
        const std::optional<std::uint64_t> system = Platform::availableMemory();
        report += system.has_value()
            ? std::format("  system memory the process may still take: {:.1f} MiB\n", mebibytes(*system))
            : std::string("  system memory the process may still take: not stated\n");

        // Reaching here proves the rest: a driver advertising an entry point it cannot dispatch fails
        // before this line rather than at the first frame that needed it.
        report += "\nlogical device and every required entry point: ok";

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

        // The world a change of mode released, built again: its reports, sum, eye and wake go on describing
        // it. Its motion does not: what moved since stands where the build found it.
        const bool rebuilt = slot.isWorld() && std::exchange(mReleasedWorld, 0) == scene.getIdentity();
        if (rebuilt)
            mDescriber.lose(FramePast::resized());

        if (slot.isWorld() && !rebuilt)
        {
            // **A limit on memory, not a matter of safety.** The drain frees what the old world just
            // buried, so a second world does not hold two of everything at once.
            drain();

            // The only place reports are dropped: an arrival or a resize keeps them, but a new world
            // starts the caller's count again and an old report would answer with the wrong frame.
            mRing.dropReports();

            // A sum over one scene means nothing over the next, and neither does any history: a motion
            // vector would point into a world that is gone, and its wake would ring on in the new water.
            mFrame.getChain().dropSum();
            mDescriber.lose(FramePast::everything());
        }

        // What the device has room for is decided below, against what it says now.
        mDevice.getMemory().refreshBudget(mDevice.getTimeline().getNext());

        // **The world's structures settle tight before its textures are made** (`BuildCompaction`): its
        // build is flushed and waited on anyway, and textures made beside loose structures were held to
        // room compaction gave back later. A picture's rides the next submit and may not wait.
        Batch setup(mDevice.getPool());
        DeviceScene& held = mScenes.hold(slot,
            std::make_unique<DeviceScene>(mDevice, setup, mScenePasses, mArrival, scene, textures, mProfile.mAnisotropy,
                scene.getRoom(), slot.isWorld() ? BuildCompaction::Settled : BuildCompaction::Streamed));

        // A picture's scene rides the next submit, as an arrival does. The world's is one submit for the
        // whole cell, made by hand rather than by the destructor, so a failed submit throws from here.
        if (!slot.isWorld())
        {
            setup.defer();
            mHandedOver = true;
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

        // An arrival does not wait for the frames in flight: its writes queue behind whatever they still
        // read and end in the barrier `orderStagedWrites` records. It opens the frame it lands in, since
        // `beginFrame` clears the timer and a zone opened before it would be forgotten.
        GpuTimer* timer = nullptr;
        if (slot.isWorld())
            timer = &mRing.begin().mTimer;

        mDevice.getMemory().refreshBudget(mDevice.getTimeline().getNext());

        Batch setup(mDevice.getPool());
        held.extend(setup, mArrival, scene, arrived, timer);
        mHandedOver = true;

        // Deferred to the placement's submit, which the caller's `placeScene` makes ahead of the refit and
        // the top level, so a composite landing costs no submit, fence or wait of its own.
        setup.defer();

        // The history is kept: nothing was renumbered, and dropping it flashes every time an actor walks
        // into view with a texture nobody has worn yet.
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
        // No scene yet is a sweep that came before the first hand-over: nothing holds images to destroy.
        if (DeviceScene* const held = mScenes.find(slot); held != nullptr)
            held->dropTextures(textures);
    }

    void VulkanRenderer::placeScene(const SceneSlot slot, const SceneDesc& scene)
    {
        DeviceScene& held = mScenes.at(slot);

        // The copy the last frame did not trace; not a parity of its own, because a frame need not place.
        const FrameSlot into = held.getSlot().next();

        // A picture of this copy recorded and carried by nothing yet is carried first
        // (`DeviceScene::pictureRides`); only three placements of one scene in a frame get here.
        if (held.pictureRides(into, mDevice.getTimeline().getNext()))
            mDevice.getPool().finishDeferred();

        // Whatever last read or wrote this copy on the queue is waited for here: usually the frame before
        // last's trace, which `collectFrame` has already waited out, so this is a comparison. Where the
        // device is behind, this is the right place for the CPU to stand still.
        held.finishReads(into);

        // **The host's rows ride a batch of their own, deferred ahead of the placement**, which the pool
        // submits first: staged copies run before the placement reads them, and mapped rows cost nothing.
        Batch written(mDevice.getPool());
        Uploads uploads(written);

        // A picture inside the interface is placed into a batch that rides the next submit, neither timed
        // nor opening the frame's report; its trace is deferred the same way and `place`'s barrier orders them.
        if (!slot.isWorld())
        {
            // Nothing recorded is nothing carried, as for the world's below.
            Batch placement(mDevice.getPool());
            const bool placed = held.place(scene,
                Placing{
                    .mCommands = placement.getCommands(),
                    .mSlot = into,
                    .mUploads = uploads,
                });
            uploads.order();
            written.defer();
            if (placed)
                placement.defer();
            else
                placement.abandon();
            held.placed(into);
            return;
        }

        // A placement opens the frame, and every placement before a trace joins it: the report starts here
        // so that the refit and the top level are counted.
        FrameRecord& frame = mRing.begin();

        // Submitted without a wait; the frame's trace, later on the queue, covers it. Nothing recorded is
        // nothing submitted, as for every frame of a standing camera in an empty place.
        Recording placement = mDevice.getPool().begin(mRing.takePlaceCommands(frame));

        const bool placed = held.place(scene,
            Placing{
                .mCommands = placement.get(),
                .mSlot = into,
                .mTimer = &frame.mTimer,
                .mUploads = uploads,
            });
        uploads.order();
        written.defer();
        if (placed)
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

    void VulkanRenderer::setVerticalSync(VerticalSync mode)
    {
        // Headless: `shot`, `bench` and `check` present to nothing, and a run with no surface has
        // no refresh to meet.
        if (mPresenter == nullptr)
            return;

        // The drain runs only where a rebuild follows: most settings change no present mode, and the
        // drain is a submit and a wait.
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

    void VulkanRenderer::setIndirectLighting(const bool traced)
    {
        mProfile.mReconstruction.mSampling.mIndirect = traced;
    }

    void VulkanRenderer::setBounceRays(const std::uint32_t rays)
    {
        assert(rays >= 1 && rays <= Shaders::MAX_BOUNCE_RAYS && "a count its reader should have refused");

        const std::uint32_t was = mProfile.mReconstruction.mSampling.mBounceRays;
        mProfile.mReconstruction.mSampling.mBounceRays = rays;

        // **The landing's own stage images are made only for more than one ray** (`landingApart`), so only
        // a move across one places the arena again.
        if ((was > 1) != (rays > 1) && mTarget.isOpen())
        {
            const VkExtent2D output = mTarget.getExtent();
            createTargets(output.width, output.height);
        }
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

        // Asked before anything is drained, because `RtxWindow::fit` calls this every settled frame;
        // remaking a swapchain waits the device idle (`Presenter::wantsResize` says what a no-op costs).
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

        // After the frame's submit, and not waited for; drawn with no batches too, since what is shown is
        // the picture under them either way.
        //
        // **Recorded into the present's own submit where one follows**, ahead of its blit, to save a second
        // head barrier and queue trip every frame. A renderer with no window submits it here.
        mGui.prepare(vertices, batches);
        if (mPresenter == nullptr)
            mGui.submit(mTarget.getPicture(), mTarget.getShown());
        mTarget.showInterface();
    }

    void VulkanRenderer::presentFrame()
    {
        assert(mPresenter != nullptr && "presentFrame on a renderer that was given no window");
        assert(mTarget.isOpen());

        if (!mTarget.isShownCurrent())
            mGui.prepare({}, {});

        // **Left where the blit reads it**, so the GUI's last barrier is the one the blit needs.
        mPresenter->present(mTarget.getShown(), Use::sBlitRead, [&](const VkCommandBuffer commands) {
            mGui.record(commands, mTarget.getPicture(), mTarget.getShown(), Use::sBlitRead);
        });
        mTarget.spendShown();
    }

    void VulkanRenderer::awaitPresented()
    {
        if (mPresenter != nullptr)
            mPresenter->awaitLatency();
    }

    FrameExtents VulkanRenderer::getExtents() const
    {
        return FrameExtents{
            .mRenderWidth = mFrame.getChain().getWidth(),
            .mRenderHeight = mFrame.getChain().getHeight(),
            .mOutputWidth = mTarget.getExtent().width,
            .mOutputHeight = mTarget.getExtent().height,
        };
    }

    JobProgress VulkanRenderer::awaitKernels(const KernelSet set, const std::chrono::milliseconds patience)
    {
        const JobProgress progress = mTracePasses.mVisibility.awaitKernels(set, patience);

        // **The cache is written the moment the start's compile is whole**, not only as the device goes, so
        // a crash later in the session keeps it. Once; what compiles after is written at exit.
        if (!mKernelsSaved
            && mTracePasses.mVisibility.awaitKernels(KernelSet::Drawing, std::chrono::milliseconds::zero()).isDone())
        {
            mDevice.savePipelineCache();
            mKernelsSaved = true;
        }

        // What the wait was for, said where it ends: a start that stood seconds on its kernels, cold or
        // past a cache that missed, reads why in the line.
        if (!mKernelsSummarised && progress.isDone())
        {
            mDevice.logPipelineSummary("Pipelines by the time the start's kernels stood ready");
            mKernelsSummarised = true;
        }
        return progress;
    }

    FrameTraced VulkanRenderer::renderFrame(const FrameRequest& request)
    {
        WorldOptions described;
        const FrameTraced traced = mDescriber.describe(request, described);
        traceFrame(traced.mConstants, request.mOptions, described);
        return traced;
    }

    void VulkanRenderer::holdAir(const AirClock& air)
    {
        mDescriber.holdAir(air);
    }

    void VulkanRenderer::traceFrame(
        const Shaders::VisibilityConstants& camera, const FrameOptions& options, const WorldOptions& described)
    {
        // A frame that handed nothing over gives the ring a block back where an arrival grew it past what
        // it keeps (`CommandPool::shedStaging`).
        if (!std::exchange(mHandedOver, false))
            mDevice.getPool().shedStaging(sStagingKept);

        const DeviceScene* const held = mScenes.find(SceneSlot::world());
        assert(held != nullptr && "renderFrame before setScene");
        const DeviceScene& world = *held;
        assert((options.mAccumulate == 0 || mProfile.mRadianceWidth == RadianceWidth::Summed)
            && "a sum of frames in a run that only shows them, whose reserve keeps no room for it");
        TraceChain& chain = mFrame.getChain();
        assert(camera.mEyes.mWorld.mWidth == chain.getWidth() && camera.mEyes.mWorld.mHeight == chain.getHeight()
            && "the camera has to be built for the render extent; ask getExtents");

        // Coverage and an upscaler do not meet: an upscaler writes the upscaled image itself and is
        // handed no coverage, so a picture that stops where nothing was hit is `traceGuiTexture`'s.
        assert((camera.mTransparentBackground == 0 || !upscaling())
            && "a frame that stops where nothing was hit belongs to traceGuiTexture, which does not upscale");

        // The frame `placeScene` opened, or a new one where nothing was placed.
        FrameRecord& frame = mRing.begin();

        const float sinceLastMs = options.mSinceLast * 1000.0f;

        // What reconstructs this frame, decided once: every switch below reads it, and the same value goes
        // back in the frame result, so what a run reports and what it did are one answer.
        const Reconstruction reconstruction
            = mProfile.reconstructionFor(mUpscaler.getInput(), options.mReconstruction, getExtents());
        frame.mReconstruction = reconstruction;

        // Every history is worthless after a jump no motion vector can describe: through a door, a
        // reprojection would fetch one room onto another. Spent here, by the frame it describes.
        const FramePast past = mDescriber.takePast(options.mLoss);
        if (past.losesAny())
            Log(Debug::Verbose) << "History reset by " << describe(past);

        Shaders::VisibilityConstants sampled = sampleFrame(
            camera, options, mProfile, reconstruction, world.getCounts(), mDescriber.reprojectingFrom(past));
        world.measureStars(sampled.mStars);

        // The rays the misses are counted against: one a pixel of the traced extent, not the shown one.
        frame.mCountedRays = mCounting ? sampled.mEyes.mWorld.mWidth * sampled.mEyes.mWorld.mHeight : 0u;

        const TraceSubject subject
            = mMedia.describe(world, camera, reconstruction.mSampling, frame.mCounts, mDisplay.getGlareCounts());

        if (past.mReprojectionLost)
            mUpscaler.reset();
        if (past.mEyeLost)
            mDisplay.loseEye();
        if (past.mWaterLost)
            mMedia.resetRipples();

        GpuTimer& timer = frame.mTimer;

        // What the trace's passes write from the host (ripple impulses, the upscaler's constants, debug
        // lines) rides a batch deferred ahead of the trace's submit, as a placement's rows do.
        Batch written(mDevice.getPool());
        Uploads uploads(written);

        Recording trace = mRing.recordWorld(frame);
        const VkCommandBuffer commands = trace.get();

        // The glare fader's query starts the frame at nothing, ahead of the trace that counts.
        mDisplay.beginGlare(commands);

        // What walked through the water, stepped before the trace reads it and only where the world
        // stands in a sea: one field under every picture of this frame.
        if (subject.mSea)
        {
            mMedia.stepRipples(commands, uploads, mRing.getRecordingSlot(),
                osg::Vec2f(camera.mOrigin.x(), camera.mOrigin.y()), described.mWaterSeconds, &timer);
            mMedia.placeRipples(sampled);
            mMedia.synthesiseSea(commands, sampled.mWaterTime, &timer);
        }

        Image* const deep = mTarget.beginPicture(mDevice, options.mAccumulate > 0);
        Image& target = mTarget.getPicture();

        const TraceResult traced = chain.record(commands,
            TraceRecording{
                .mSubject = subject,
                .mBinSlot = mRing.getRecordingSlot(),
                .mAsked = BinCamera::of(camera),
                .mSampled = sampled,
                .mReconstruction = reconstruction,
                .mExposure = mDisplay.getFrameExposure(),
                .mAccumulate = options.mAccumulate,
                .mPastLost = past.mReprojectionLost,
                .mTimer = &timer,
            });
        const GBuffer& channels = traced.mInputs.mChannels;

        // Before anything past the trace has a say; `FrameDigest` says why the picture is not enough.
        if (options.mReadBack || mRing.keepsNotFinite())
        {
            // **The direct channel's slot holds the composed frame**: the picture, in the frame's own
            // order, which is what two runs of one frame are held to (`FrameDigest`).
            std::array<const Image*, Shaders::DIGEST_IMAGES> digested{};
            std::uint32_t inFields = 0;
            for (const Channel channel : sEveryChannel)
            {
                const bool composed = channel == Channel::Direct;
                // A channel kept a texel wide holds nothing of the frame's (`ChannelKeeping`).
                digested[indexOf(channel)] = composed ? &chain.getColour()
                    : channels.keepsWhole(channel)    ? &channels.get(channel)
                                                      : nullptr;
                inFields |= !composed && storedInFields(channel) ? 1u << indexOf(channel) : 0u;
            }

            std::array<const Image*, sDenoiserImageCount> denoiser{};
            chain.denoiserWritten(denoiser);

            const DigestImages images(
                digested, inFields, denoiser, VkExtent2D{ sampled.mEyes.mWorld.mWidth, sampled.mEyes.mWorld.mHeight });
            if (options.mReadBack)
                mRing.readDigest(frame, commands, images, sampled.mFrame,
                    FrameDigest{
                        .mJitterX = sampled.mEyes.mWorld.mJitter.x(),
                        .mJitterY = sampled.mEyes.mWorld.mJitter.y(),
                        .mFrameDeltaMs = sinceLastMs,
                        .mReset = reconstruction.upscaled() && mUpscaler.isFresh() ? 1u : 0u,
                    },
                    &timer);
            if (mRing.keepsNotFinite())
                mRing.keepNotFinite(commands, images, sampled.mFrame);
        }

        // The rest of the frame runs over the reconstruction where something upscales and over the trace's
        // own composite where nothing does; the picture is the output's extent either way.
        const HandedImage shown = [&] {
            if (!reconstruction.upscaled())
                return traced.mColour;

            return mUpscaler.record(commands,
                UpscaleInputs{
                    .mColour = traced.mColour,
                    .mSurface = channels.get(Channel::Surface),
                    .mMotion = channels.get(Channel::Motion),
                    .mAlbedo = channels.get(Channel::Albedo),
                    .mSpecularAlbedo = channels.get(Channel::SpecularAlbedo),
                    .mSpecular = channels.get(Channel::Specular),
                    .mLobed = traced.mInputs.mSubject.mMapped,
                    .mMasks = channels.get(Channel::UpscaleMasks),
                    .mEyes = sampled.mEyes,
                    .mPreviousJitter = sampled.mPreviousJitter,
                    .mFrame = sampled.mFrame,
                    .mJitterPhases = reconstruction.mJitterPhases,
                    .mSeconds = options.mSinceLast,
                },
                &timer);
        }();

        mShownRadiance = &mDisplay.record(commands,
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
                    .mSeconds = options.mWorldSinceLast,
                    .mGlare = described.mGlare,
                    .mInverseGamma = mInverseGamma,
                    .mNightEye = described.mNightEye,
                    .mDither = options.mDither.value_or(mProfile.mDither),
                    .mDeep = deep,
                    .mDebug = options.mDebug,
                    .mDebugVertices = frame.mDebugVertices,
                    .mUploads = uploads,
                    .mTimer = timer,
                },
            });

        // The picture as the curve left it and before the interface.
        if (options.mReadBack)
            mRing.readPicture(frame, commands, target);

        // After the picture and inside the frame's trace, so the hold is the last thing the frame did.
        if (mStress != nullptr)
            mStress->record(commands, frame.mCounts, &timer);

        // Submitted and not waited for: `finishFrame` or `collectFrame` brings the counts and the
        // report back a frame or two late.
        uploads.order();
        written.defer();
        mRing.submit(frame, std::move(trace));

        mDescriber.traced(camera, sampled);
    }

    void VulkanRenderer::reservePicture(const std::uint32_t width, const std::uint32_t height)
    {
        assert(width > 0 && height > 0);
        if (width <= mPictureRoom.width && height <= mPictureRoom.height)
            return;

        // **Made with the frame's targets and counted with them**, so the room content stands in is what
        // is left beside them.
        mPictureRoom = VkExtent2D{ std::max(mPictureRoom.width, width), std::max(mPictureRoom.height, height) };
        const VkExtent2D output = mTarget.getExtent();
        createTargets(output.width, output.height);
    }

    SceneSlot VulkanRenderer::addViewScene()
    {
        return mScenes.add();
    }

    void VulkanRenderer::dropViewScene(const SceneSlot scene)
    {
        // Not drained: the scene's objects bury themselves, and a drain idles the whole device.
        mScenes.drop(scene);
    }

    bool VulkanRenderer::traceGuiTexture(
        const GuiSlot texture, const SceneSlot scene, const Viewpoint& view, const GuiTraceOptions& options)
    {
        return tracePicture(texture, scene, FrameDescriber::describePicture(view, options), options);
    }

    bool VulkanRenderer::tracePicture(const GuiSlot texture, const SceneSlot scene,
        const Shaders::VisibilityConstants& camera, const GuiTraceOptions& options)
    {
        assert(mGui.getTextures().holds(texture) && "a trace into a slot nothing holds");

        const VkExtent2D extent{ camera.mEyes.mWorld.mWidth, camera.mEyes.mWorld.mHeight };
        if (extent.width == 0 || extent.height == 0)
            return false;

        assert(mPictures.holds(extent) && "a picture past the room its view reserved");
        mPictures.trace(texture, scene, camera, options, mScenes.at(scene), mProfile,
            PictureTracer::Parts{ .mMedia = mMedia, .mDisplay = mDisplay, .mTextures = mGui.getTextures() });
        return true;
    }

    bool VulkanRenderer::takeGuiCopy(const GuiSlot texture, const std::span<std::uint8_t> into)
    {
        return mGui.getTextures().takeCopy(texture, into);
    }

    void VulkanRenderer::finishGuiTraces()
    {
        drain();
    }

    void VulkanRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        assert(mTarget.isOpen());

        const Image& picture = mTarget.getPicture();
        pixels.resize(picture.getReadBytes());
        picture.read(VK_IMAGE_LAYOUT_GENERAL, mReadBack, std::as_writable_bytes(std::span(pixels)));
    }

    std::optional<NotFiniteFrame> VulkanRenderer::takeNotFiniteFrame()
    {
        return mRing.takeNotFiniteFrame();
    }

    void VulkanRenderer::readDeepPixels(std::vector<std::uint16_t>& samples)
    {
        const Image& deep = mTarget.getDeep();
        const VkDeviceSize bytes = deep.getReadBytes();
        assert(bytes % sizeof(std::uint16_t) == 0 && "a deep picture of a texel no whole number of samples");
        samples.resize(bytes / sizeof(std::uint16_t));
        deep.read(VK_IMAGE_LAYOUT_GENERAL, mReadBack, std::as_writable_bytes(std::span(samples)));
    }
}
