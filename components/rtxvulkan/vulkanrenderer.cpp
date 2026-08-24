#include "vulkanrenderer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstring>

#include <components/rtx/camera.hpp>
#include <components/rtx/error.hpp>
#include <components/rtx/scenedesc.hpp>

#ifdef OPENMW_RTX_DLSS
#include "dlss.hpp"
#include "dlsspass.hpp"
#endif

#include "gbuffer.hpp"
#include "image.hpp"
#include "physicaldevice.hpp"
#include "presenter.hpp"
#include "requirements.hpp"
#include "sceneacceleration.hpp"
#include "scenebuffers.hpp"
#include "texture.hpp"
#include "visibilitypass.hpp"

namespace Rtx
{
    namespace
    {
        /// The instance a window needs, which is the headless one plus whatever SDL asks for.
        InstanceOptions instanceOptionsFor(const RendererOptions& options)
        {
            InstanceOptions instance = toInstanceOptions(options.mValidation);
            if (options.mWindow != nullptr)
                instance.mSurfaceExtensions = Presenter::getInstanceExtensions(options.mWindow);

            return instance;
        }

        /// A swapchain is the only thing presenting adds to the device.
        std::vector<const char*> deviceExtensionsFor(const RendererOptions& options)
        {
            if (options.mWindow == nullptr)
                return {};

            return { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        }
    }

    /// Everything a picture that is not of the world needs to be traced against.
    struct ViewScene
    {
        std::unique_ptr<SceneAcceleration> mAcceleration;
        std::unique_ptr<SceneBuffers> mBuffers;
        std::unique_ptr<TextureArray> mTextures;
    };

    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mInstance(instanceOptionsFor(options))
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle()), deviceExtensionsFor(options))
        , mPool(mDevice)
        , mTimer(mDevice)
        , mShaderDirectory(options.mShaderDirectory)
        , mUpscale(options.mUpscale)
        , mFilter(mDevice, options.mShaderDirectory)
        , mViewFilter(mDevice, options.mShaderDirectory)
        , mComposite(mDevice, mPool, options.mShaderDirectory)
        , mExposure(mDevice, options.mShaderDirectory)
        , mTone(mDevice, options.mShaderDirectory)
        , mGuiPass(mDevice, options.mShaderDirectory, sTargetFormat)
        , mGuiTextures(mDevice, mPool)
    {
        mHitCount = Buffer(mDevice, sizeof(std::uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // Before the first targets, because what to trace at is its answer and not ours.
        if (mUpscale != Upscale::Off)
        {
#ifdef OPENMW_RTX_DLSS
            mNgx = Dlss::open(mDevice, mInstance.getHandle());
            if (!mNgx->isAvailable())
                throw Error("DLSS Ray Reconstruction was asked for and " + mNgx->getObstacle());
#else
            // **Named rather than quietly ignored.** A build that cannot upscale and renders at the
            // output size anyway is one whose frame times mean something else entirely.
            throw Error(
                "upscaling was asked for and this build has no DLSS; configure with "
                "-DOPENMW_RTX_DLSS=ON");
#endif
        }

        // Before the first targets, because a windowed renderer is sized by its surface rather
        // than by what the caller guessed the window would come up at.
        if (options.mWindow != nullptr)
            mPresenter = std::make_unique<Presenter>(mDevice, mInstance.getHandle(), options.mWindow);

        const VkExtent2D output
            = mPresenter != nullptr ? mPresenter->getExtent() : VkExtent2D{ options.mWidth, options.mHeight };
        createTargets(output.width, output.height);
    }

    // Out of line because the members it destroys are only forward declared in the header.
    VulkanRenderer::~VulkanRenderer() = default;

    void VulkanRenderer::createTargets(std::uint32_t width, std::uint32_t height)
    {
        assert(width > 0 && height > 0);

        mOutputWidth = width;
        mOutputHeight = height;

        // Whatever upscales picks the render size; without one the two extents are the same number
        // twice, and every pass below is written as though they always might not be.
        VkExtent2D render{ width, height };
#ifdef OPENMW_RTX_DLSS
        if (mNgx != nullptr)
            render = mNgx->getRenderSize(VkExtent2D{ width, height }, mUpscale);
#endif
        mRenderWidth = render.width;
        mRenderHeight = render.height;

        // `SAMPLED` because an upscaler samples what it is handed, and one bit short of that is a
        // black frame nothing reports. See `GBuffer`, which carries it for the same reason.
        mColour = std::make_unique<Image>(mDevice, mRenderWidth, mRenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "colour");

        // **Always shareable, not only when something asks.** The only cost is which memory type
        // the driver picks, and it does not move the frame: Balmora at 1080p, best of thirty, reads
        // 6.19 ms shareable against 6.10 to 6.26 across four runs before it, and the picture is
        // byte-identical. The alternative is an option every caller has to know to set before the
        // frame it wants can leave the device.
        mTarget = std::make_unique<Image>(mDevice, mOutputWidth, mOutputHeight, sTargetFormat,
            // Drawn into as well as written: the tone curve writes it as a storage image and the GUI
            // rasterises over what that left.
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
                | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            "target", Sharing::Exportable);

        // **Black and in `GENERAL` from the moment it exists.** Everything that reads the target
        // expects that layout, and the GUI is drawn over the target whether or not a frame has been
        // traced into it — a main menu and a loading screen have no world behind them.
        mPool.submitAndWait([&](VkCommandBuffer commands) {
            mTarget->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);

            const VkClearColorValue black{ .float32 = { 0.0f, 0.0f, 0.0f, 1.0f } };
            const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            vkCmdClearColorImage(
                commands, mTarget->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &whole);

            mTarget->transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT);
        });

        mChannels = std::make_unique<GBuffer>(mDevice, mRenderWidth, mRenderHeight);
        mFilter.resize(mRenderWidth, mRenderHeight);

#ifdef OPENMW_RTX_DLSS
        // Released before the next is built: the feature holds the network's weights for one pair
        // of resolutions, which is most of what it occupies.
        mUpscaler.reset();

        if (mNgx != nullptr)
        {
            mUpscaled = std::make_unique<Image>(mDevice, mOutputWidth, mOutputHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "upscaled");

            mNoSpecular = std::make_unique<Image>(mDevice, mRenderWidth, mRenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                "no-specular");

            // Building uploads the network's weights, and the zero specular albedo is cleared in the
            // same submit — both are once per resolution rather than once per frame.
            mPool.submitAndWait([&](VkCommandBuffer commands) {
                mNoSpecular->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);

                const VkClearColorValue nothing{};
                const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                vkCmdClearColorImage(
                    commands, mNoSpecular->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &nothing, 1, &whole);

                mNoSpecular->transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_READ_BIT);

                mUpscaler = std::make_unique<DlssPass>(
                    *mNgx, commands, render, VkExtent2D{ mOutputWidth, mOutputHeight }, mUpscale);
            });
        }
#endif

        // A frame of a different size is not one this one can be reprojected against.
        mPreviousCamera = Shaders::VisibilityConstants{};

        // **Dropped rather than resized, because most runs never make one.** Sixteen bytes a pixel
        // is 33 MiB at 1080p and 133 MiB at 4K, and it buys a sum that neither rounds nor clips —
        // which is worth every byte to the reference mode and nothing at all to the frame a window
        // or a plain shot draws. The first averaging frame is what asks for it.
        mHistory.reset();
    }

    std::string VulkanRenderer::describeDevice() const
    {
        std::string report = "loader:            Vulkan " + versionString(mInstance.getApiVersion()) + '\n'
            + "validation:        " + (mInstance.getValidationLog() != nullptr ? "on" : "off") + '\n'
            + "debug utils:       " + (mInstance.hasDebugUtils() ? "on" : "off") + '\n';

        report += mDevice.getPhysicalDevice().describe();

#ifdef OPENMW_RTX_DLSS
        report += "\nDLSS Ray Reconstruction: ";
        try
        {
            // **A share of whatever is up, which under a renderer that upscales is its own.** This
            // used to build a `Dlss` of its own to ask with and let it go again; NGX keeps one
            // runtime per process and its shutdown is unconditional, so that second one ended the
            // first the moment it left scope — and what that looked like was this renderer's
            // upscaler refusing a frame a cell load later with `FAIL_NotInitialized`, pointing at
            // nothing. `Dlss::open` is why the same line is now safe.
            const std::shared_ptr<const Dlss> ngx = Dlss::open(mDevice, mInstance.getHandle());
            report += ngx->isAvailable() ? "available\n" : "unavailable, " + ngx->getObstacle() + "\n";
        }
        catch (const Error& error)
        {
            report += std::string("unavailable, ") + error.what() + '\n';
        }
#else
        report += "\nDLSS Ray Reconstruction: not built in; configure with -DOPENMW_RTX_DLSS=ON\n";
#endif

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

    void VulkanRenderer::setScene(const SceneDesc& scene, std::span<const TextureData> textures, const SeaState& sea)
    {
        // Torn down before anything is built, so a second scene does not hold two of everything at
        // once — a cell's structures and textures are most of what this renderer occupies. The pass
        // is not among them; see below.
        mTextures.reset();
        mBuffers.reset();
        mAcceleration.reset();

        // A sum over one scene means nothing over the next, so it goes back with the scene rather
        // than being carried empty into one it cannot describe. Neither does a motion vector, which
        // would point at where something stood in a world that is no longer there.
        mHistory.reset();
        mPreviousCamera = Shaders::VisibilityConstants{};

        // Whatever a previous frame's placement recorded belongs to a scene that no longer exists.
        mTimed = false;

        // Made here for the same reason a frame's are: both of the two below want them, and this is
        // the only place that knows both.
        makeInstanceRecords(scene, mRecordScratch);

        // **One submit for the whole cell.** Every structure, every table and every texture is
        // recorded into this and the queue is asked once, at the flush below; each of them used to
        // be its own round trip, and Balmora's are 367 of them.
        Batch setup(mPool);

        mAcceleration = std::make_unique<SceneAcceleration>(mDevice, setup, scene, mRecordScratch);
        mBuffers = std::make_unique<SceneBuffers>(mDevice, setup, scene, mRecordScratch, sea);
        mTextures = std::make_unique<TextureArray>(mDevice, setup, textures);
        mBuiltMeshes = scene.getMeshRevision();

        // **Built once and kept, because building one compiles the shader — half a second a time,
        // measured.** Nothing about the pass depends on the scene: it needs the device and the shape
        // of the texture set, and every array declares that shape identically — the bindless binding
        // is sized to its maximum rather than to the cell, so what varies between scenes is how many
        // descriptors get allocated and never what the layout says. Identically defined layouts are
        // compatible, so a set from a later array binds against the pipeline layout the first one
        // produced. `TextureArray`'s layout is where that invariant is kept.
        if (mPass == nullptr)
            mPass = std::make_unique<VisibilityPass>(mDevice, setup, mShaderDirectory, mTextures->getLayout());

        // By hand rather than left to the destructor, so a submit that fails throws out of here
        // instead of being logged on the way past.
        setup.flush();

        mStats = SceneStats{
            .mInstances = mAcceleration->getInstanceCount(),
            .mCutoutInstances = mAcceleration->getCutoutInstanceCount(),
            .mStructureBytes = mAcceleration->getStructureBytes(),
            .mTableBytes = mBuffers->getBytes(),
            .mTextureCount = mTextures->getCount(),
            .mTextureBytes = mTextures->getBytes(),
        };
    }

    void VulkanRenderer::extendScene(const SceneDesc& scene, std::span<const TextureData> arrived, const SeaState& sea)
    {
        assert(mAcceleration != nullptr && "extendScene before setScene");

        Batch setup(mPool);
        mTextures->write(setup, arrived);

        // **The meshes that arrived, and no others.** Everything already built stays where it is:
        // the geometry blocks are appended to rather than replaced, so every address a structure was
        // built from is still its own, and the storage a departing mesh gives back goes to the next
        // one that fits.
        //
        // **The revision and not the count.** A slot a departing cell freed is taken over by the
        // next mesh that fits, so the table can hold different geometry at the same size — and a
        // guard on the size would send that here without noticing.
        if (scene.getMeshRevision() != mBuiltMeshes)
        {
            mBuffers->extend(scene);
            mAcceleration->extend(setup, scene);
            mBuiltMeshes = scene.getMeshRevision();
        }

        // **Flushed before the placement, not after it.** `placeScene` submits on its own and refits
        // structures the arrivals above may have just built, so the two cannot be left to finish in
        // whatever order their destructors run in.
        setup.flush();

        // Always, because the top level names every instance and an arrival changed the list. It is
        // rebuilt every frame regardless, so an arrival costs it nothing.
        placeScene(scene, sea);

        // **The history is kept.** Nothing was renumbered, so what the last frame resolved still
        // describes the same surfaces — and throwing it away is a visible flash every time an actor
        // walks into view with a texture nobody has worn yet.
        mStats = SceneStats{
            .mInstances = mAcceleration->getInstanceCount(),
            .mCutoutInstances = mAcceleration->getCutoutInstanceCount(),
            .mStructureBytes = mAcceleration->getStructureBytes(),
            .mTableBytes = mBuffers->getBytes(),
            .mTextureCount = mTextures->getCount(),
            .mTextureBytes = mTextures->getBytes(),
        };
    }

    std::uint32_t VulkanRenderer::getTextureCount() const
    {
        return mTextures == nullptr ? 0 : mTextures->getCount();
    }

    void VulkanRenderer::dropTextures(std::span<const std::uint32_t> slots)
    {
        // Before there is an array at all, which is a scene that swept before it was ever handed
        // over. There is nothing holding the images to destroy.
        if (mTextures == nullptr)
            return;

        mTextures->drop(slots);
    }

    void VulkanRenderer::placeScene(const SceneDesc& scene, const SeaState& sea)
    {
        assert(mAcceleration != nullptr && "placeScene before setScene");

        // **The frame's report starts here and not at the trace.** Placing the world is two submits
        // and, on a nine-by-nine exterior, most of the frame; a report that began at `renderFrame`
        // would leave the largest part of it out.
        mTimer.beginFrame();
        mTimed = true;

        // **Once, and both halves read it.** The rows carry a matrix inverse apiece and a
        // nine-by-nine exterior is fifty thousand of them; the acceleration structure and the
        // instance table were each building the whole set for themselves.
        makeInstanceRecords(scene, mRecordScratch);

        mAcceleration->place(mPool, scene, mRecordScratch, &mTimer);

        // **Only what a moving world changed**, which is the instance rows, the lights and the
        // vertices of anything skinned. Rebuilding all of it was measured at twenty to twenty-seven
        // milliseconds on a nine-by-nine region and was the largest single cost in the frame.
        mBuffers->place(scene, mRecordScratch, sea);

        mStats.mInstances = mAcceleration->getInstanceCount();
        mStats.mCutoutInstances = mAcceleration->getCutoutInstanceCount();
        mStats.mTableBytes = mBuffers->getBytes();

        // A frame whose instances moved is not one the last frame reprojects onto.
        mHistory.reset();
    }

    void VulkanRenderer::resize(std::uint32_t width, std::uint32_t height)
    {
        if (mPresenter != nullptr)
        {
            // **What the swapchain came back with, not what was asked for.** A surface clamps to
            // what it can do, and targets sized to the request would then be blitted through a
            // scale nobody chose.
            mPresenter->resize(VkExtent2D{ width, height });

            const VkExtent2D shown = mPresenter->getExtent();
            width = shown.width;
            height = shown.height;
        }

        if (width == mOutputWidth && height == mOutputHeight)
            return;

        // The images about to be replaced may still be in flight.
        mDevice.waitIdle();
        createTargets(width, height);
    }

    SharedFrame VulkanRenderer::shareFrame()
    {
        assert(mTarget != nullptr);

        return SharedFrame{ .mMemory = mTarget->exportMemory(), .mBytes = mTarget->getMemoryBytes() };
    }

    std::uint32_t VulkanRenderer::addGuiTexture(std::uint32_t width, std::uint32_t height)
    {
        return mGuiTextures.add(width, height);
    }

    void VulkanRenderer::writeGuiTexture(std::uint32_t texture, std::span<const std::uint8_t> rgba)
    {
        mGuiTextures.write(texture, rgba);
    }

    void VulkanRenderer::dropGuiTexture(std::uint32_t texture)
    {
        mGuiTextures.drop(texture);
    }

    void VulkanRenderer::drawGui(std::span<const GuiVertex> vertices, std::span<const GuiBatch> batches)
    {
        assert(mTarget != nullptr);

        if (vertices.empty() || batches.empty())
            return;

        if (mGuiVertices.getSize() < vertices.size_bytes())
            mGuiVertices = HostBuffer(mDevice, vertices.size_bytes(), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

        mGuiVertices.write(vertices);

        mGuiDraws.clear();
        mGuiDraws.reserve(batches.size());
        for (const GuiBatch& batch : batches)
        {
            const VkImageView view = mGuiTextures.getView(batch.mTexture);
            assert(view != VK_NULL_HANDLE && "a batch names a texture this renderer does not hold");

            // A slot nothing holds would be a null descriptor, which is undefined rather than
            // blank. The assert above is where a caller finds out; a release build drops the batch.
            if (view != VK_NULL_HANDLE)
                mGuiDraws.push_back(GuiDraw{ view, batch.mFirstVertex, batch.mVertexCount,
                    batch.mBlend == GuiBlend::Additive ? Blend::Additive : Blend::Over });
        }

        // **Its own submit, after the frame's.** The GUI is collected once the world has been drawn
        // and there is nothing to gain by holding the frame open for it; what it costs is one more
        // queue submit on the frames the interface is up, and `.notes/rtx/plan.md` §12 is where that
        // number goes once there is a frame to measure it in.
        mPool.submitAndWait([&](VkCommandBuffer commands) {
            mTarget->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

            mGuiPass.record(commands, *mTarget, mGuiVertices.getHandle(), mGuiDraws);

            // Back where everything else expects it: the presenter blits out of `GENERAL` and so
            // does a read back.
            mTarget->transition(commands, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);
        });
    }

    bool VulkanRenderer::presentFrame()
    {
        assert(mPresenter != nullptr && "presentFrame on a renderer that was given no window");
        assert(mTarget != nullptr);

        return mPresenter->present(*mTarget);
    }

    FrameExtents VulkanRenderer::getExtents() const
    {
        return FrameExtents{
            .mRenderWidth = mRenderWidth,
            .mRenderHeight = mRenderHeight,
            .mOutputWidth = mOutputWidth,
            .mOutputHeight = mOutputHeight,
        };
    }

    FrameResult VulkanRenderer::renderFrame(const Shaders::VisibilityConstants& camera, const FrameOptions& options)
    {
        assert(mPass != nullptr && "renderFrame before setScene");
        assert(camera.mWidth == mRenderWidth && camera.mHeight == mRenderHeight
            && "the camera has to be built for the render extent; ask getExtents");

        // The count is an atomic sum over the frame, so it starts each one at nothing.
        *static_cast<std::uint32_t*>(mHitCount.map()) = 0;
        mHitCount.unmap();

        // The camera as the caller wrote it, plus where in the pixel this frame samples. Filled
        // here rather than by the caller because the sequence belongs to the frame index, which is
        // the renderer's to walk.
        // **An upscaler always jitters, whatever was asked for.** Reconstruction across several
        // frames of the same sample point is reconstruction from one sample, and there is nothing in
        // `FrameOptions` a caller could set that would make that a reasonable frame to produce.
        const bool upscaling = mUpscale != Upscale::Off;

        Shaders::VisibilityConstants sampled = camera;
        if (options.mJitter || upscaling)
            sampled.mJitter = haltonJitter(camera.mFrame);

        // **The one subtraction of two world points, and it happens here.** Two camera positions a
        // step apart subtract exactly in a float; the same difference taken on the device, between
        // coordinates six figures long, would be rounding.
        sampled.mCameraMotion = camera.mOrigin - mPreviousCamera.mOrigin;
        sampled.mPreviousForward = mPreviousCamera.mForward;
        sampled.mPreviousRight = mPreviousCamera.mRight;
        sampled.mPreviousUp = mPreviousCamera.mUp;

        const VisibilityInputs inputs{
            .mScene = mAcceleration->getTopLevel(),
            .mBuffers = mBuffers.get(),
            .mIndexBlocks = mAcceleration->getIndexBlocks(),
            .mTextures = mTextures->getSet(),
            .mShading = mTextures->getShading(),
        };

        // Made by the first frame that averages, and that frame is the one that fills it.
        const bool fresh = options.mAccumulate > 0 && mHistory == nullptr;
        if (fresh)
            mHistory = std::make_unique<Image>(mDevice, mRenderWidth, mRenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT, "history");

        // A frame nothing moved for opens its own report; one that placed the world is adding to
        // the report `placeScene` started.
        if (!mTimed)
            mTimer.beginFrame();
        mTimed = false;

        const auto start = std::chrono::steady_clock::now();

        mPool.submitAndWait([&](VkCommandBuffer commands) {
            // All written whole before anything reads them, so none needs its contents carried over
            // from the last frame.
            for (const Image* image : { mColour.get(), mTarget.get() })
                image->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

#ifdef OPENMW_RTX_DLSS
            if (mUpscaled != nullptr)
                mUpscaled->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                    VK_ACCESS_2_MEMORY_WRITE_BIT);
#endif

            // The first write needs no contents and nothing to wait on; every one after reads what
            // the last left, which the fence orders and does not make visible.
            if (mHistory != nullptr)
                mHistory->transition(commands, fresh ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_GENERAL,
                    fresh ? VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    fresh ? 0 : VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            mChannels->begin(commands);
            mTimer.open(commands, "trace");
            mPass->record(commands, inputs, *mChannels, mHitCount, sampled);
            mTimer.close(commands);
            mChannels->handOver(commands);

            // Where the bounce ended up: the filter's last level, or the channel the trace wrote
            // when nothing filtered it. **Ray Reconstruction is itself the denoiser**, and handing
            // it a frame the wavelet already blurred is asking it to recover what was thrown away.
            const bool filtering = options.mFilter && !upscaling;
            if (filtering)
                mTimer.open(commands, "filter");

            const Image& indirect
                = filtering ? mFilter.record(commands, *mChannels, sampled) : mChannels->getIndirect();
            if (filtering)
                mTimer.close(commands);

            mTimer.open(commands, "composite");
            mComposite.record(commands, *mChannels, indirect, mHistory.get(), *mColour,
                Shaders::CompositeConstants{
                    .mWidth = mRenderWidth,
                    .mHeight = mRenderHeight,
                    .mAccumulate = options.mAccumulate,
                });
            mTimer.close(commands);

            // Whatever comes next reads what the composite just wrote.
            mColour->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);

            const Image* shown = mColour.get();

#ifdef OPENMW_RTX_DLSS
            if (mUpscaler != nullptr)
            {
                mTimer.open(commands, "upscale");
                mUpscaler->record(commands,
                    DlssInputs{
                        .mColour = *mColour,
                        // **A stand-in, and one worth naming.** What Ray Reconstruction wants is the
                        // albedo alone; what this is, is the albedo times whatever the water and the
                        // air took on the way to the eye. The two part company in fog, and they stop
                        // parting company when there is a material model to separate them.
                        .mDiffuseAlbedo = mChannels->getModulate(),
                        .mSpecularAlbedo = *mNoSpecular,
                        .mNormalRoughness = mChannels->getGuide(),
                        .mDepth = mChannels->getDepth(),
                        .mMotion = mChannels->getMotion(),
                        .mOutput = *mUpscaled,
                        .mJitter = sampled.mJitter,
                        // No previous frame to reproject against is exactly what a reset means, and
                        // a zero basis is how the trace already spells it.
                        .mReset = mPreviousCamera.mForward.length2() <= 0.0f,
                    });

                // What NGX recorded is its own; nothing here knows which stages it used.
                mUpscaled->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_WRITE_BIT,
                    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

                mTimer.close(commands);
                shown = mUpscaled.get();
            }
#endif

            // **Measured off the image the curve is about to map**, which is the upscaled one
            // wherever something upscales — see `histogram.comp` for what measuring the other one
            // costs. One `shown` feeds both, so the two cannot come apart.
            mTimer.open(commands, "exposure");
            if (options.mExposure.has_value())
                mExposure.recordFixed(commands, *options.mExposure);
            else
                mExposure.record(commands, *shown);
            mTimer.close(commands);

            mTimer.open(commands, "tone");
            mTone.record(commands, *shown, mExposure.getExposure(), *mTarget, mOutputWidth, mOutputHeight);
            mTimer.close(commands);
        });

        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        // What the next frame reprojects against, and the camera as the caller gave it: a jitter is
        // where inside a pixel this frame sampled, not where the eye was.
        mPreviousCamera = camera;

        const std::uint32_t hits = *static_cast<const std::uint32_t*>(mHitCount.map());
        mHitCount.unmap();

        // Read after the last of the frame's submits has been waited on, which every one of them
        // has: the pool fences each before it returns.
        return FrameResult{ .mHits = hits, .mTraceMs = ms, .mGpu = mTimer.resolve() };
    }

    std::uint32_t VulkanRenderer::addViewScene()
    {
        if (!mFreeViewScenes.empty())
        {
            const std::uint32_t slot = mFreeViewScenes.back();
            mFreeViewScenes.pop_back();
            mViewScenes[slot] = std::make_unique<ViewScene>();
            return slot;
        }

        mViewScenes.push_back(std::make_unique<ViewScene>());
        return static_cast<std::uint32_t>(mViewScenes.size() - 1);
    }

    void VulkanRenderer::setViewScene(std::uint32_t scene, const SceneDesc& desc, std::span<const TextureData> textures)
    {
        assert(scene < mViewScenes.size() && mViewScenes[scene] != nullptr && "a scene nothing holds");

        ViewScene& held = *mViewScenes[scene];

        // Torn down before anything is built, for the reason `setScene` gives: holding two of
        // everything at once is what a rebuild is trying not to cost.
        held.mTextures.reset();
        held.mBuffers.reset();
        held.mAcceleration.reset();

        makeInstanceRecords(desc, mRecordScratch);

        Batch setup(mPool);

        held.mAcceleration = std::make_unique<SceneAcceleration>(mDevice, setup, desc, mRecordScratch);
        held.mBuffers = std::make_unique<SceneBuffers>(mDevice, setup, desc, mRecordScratch, SeaState{});
        held.mTextures = std::make_unique<TextureArray>(mDevice, setup, textures);

        // A doll can be the first thing this renderer ever builds — a race preview stands in front
        // of a game that has no world yet — and the pass belongs to neither scene.
        if (mPass == nullptr)
            mPass = std::make_unique<VisibilityPass>(mDevice, setup, mShaderDirectory, held.mTextures->getLayout());

        setup.flush();
    }

    void VulkanRenderer::dropViewScene(std::uint32_t scene)
    {
        assert(scene < mViewScenes.size() && mViewScenes[scene] != nullptr && "a scene given back twice");

        mViewScenes[scene].reset();
        mFreeViewScenes.push_back(scene);
    }

    void VulkanRenderer::growViewTargets(std::uint32_t width, std::uint32_t height)
    {
        if (mViewTarget != nullptr && width <= mViewWidth && height <= mViewHeight)
            return;

        // Each axis to the larger of what was there and what is wanted, so a wide picture after a
        // tall one does not throw the tall one's height away and build it again next time.
        mViewWidth = std::max(mViewWidth, width);
        mViewHeight = std::max(mViewHeight, height);

        mViewColour = std::make_unique<Image>(
            mDevice, mViewWidth, mViewHeight, VK_FORMAT_R32G32B32A32_SFLOAT, VK_IMAGE_USAGE_STORAGE_BIT, "view colour");

        mViewTarget = std::make_unique<Image>(mDevice, mViewWidth, mViewHeight, sTargetFormat,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "view target");

        mViewChannels = std::make_unique<GBuffer>(mDevice, mViewWidth, mViewHeight);
        mViewFilter.resize(mViewWidth, mViewHeight);
    }

    void VulkanRenderer::traceGuiTexture(
        std::uint32_t texture, const Shaders::VisibilityConstants& camera, const GuiTraceOptions& options)
    {
        assert(mPass != nullptr && "traceGuiTexture before any scene was built");
        assert(camera.mWidth == options.mWidth && camera.mHeight == options.mHeight
            && "the camera has to be built for the part of the texture it fills");

        const bool ofTheWorld = options.mScene == GuiTraceOptions::sWorld;
        assert((ofTheWorld || (options.mScene < mViewScenes.size() && mViewScenes[options.mScene] != nullptr))
            && "a trace against a scene nothing holds");

        const Image* into = mGuiTextures.getImage(texture);
        assert(into != nullptr && "a trace into a slot nothing holds");
        assert(options.mWidth <= into->getWidth() && options.mHeight <= into->getHeight());

        if (into == nullptr || options.mWidth == 0 || options.mHeight == 0)
            return;

        growViewTargets(options.mWidth, options.mHeight);

        const SceneAcceleration& acceleration
            = ofTheWorld ? *mAcceleration : *mViewScenes[options.mScene]->mAcceleration;
        const SceneBuffers* buffers = ofTheWorld ? mBuffers.get() : mViewScenes[options.mScene]->mBuffers.get();
        const TextureArray& array = ofTheWorld ? *mTextures : *mViewScenes[options.mScene]->mTextures;

        const VisibilityInputs inputs{
            .mScene = acceleration.getTopLevel(),
            .mBuffers = buffers,
            .mIndexBlocks = acceleration.getIndexBlocks(),
            .mTextures = array.getSet(),
            .mShading = array.getShading(),
        };

        // **Not counted, and not timed.** The hit count and the frame report are the frame's; a
        // picture drawn between two of them would overwrite both. The buffer is still bound because
        // the shader writes it whatever anyone does with the number.
        mPool.submitAndWait([&](VkCommandBuffer commands) {
            for (const Image* image : { mViewColour.get(), mViewTarget.get() })
                image->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

            mViewChannels->begin(commands);
            mPass->record(commands, inputs, *mViewChannels, mHitCount, camera);
            mViewChannels->handOver(commands);

            const Image& indirect = mViewFilter.record(commands, *mViewChannels, camera);

            mComposite.record(commands, *mViewChannels, indirect, nullptr, *mViewColour,
                Shaders::CompositeConstants{
                    .mWidth = options.mWidth,
                    .mHeight = options.mHeight,
                });

            mViewColour->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

            // **One, and measured off nothing.** A picture inside the interface is looked at beside
            // the widgets around it, and an exposure that drifted with what the doll was wearing
            // would make the same armour a different brightness in two windows.
            mExposure.recordFixed(commands, 1.0f);
            mTone.record(
                commands, *mViewColour, mExposure.getExposure(), *mViewTarget, options.mWidth, options.mHeight);

            mViewTarget->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            into->transition(commands, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT);

            // **Cleared whole and then covered in part**, and only where the picture does not cover
            // it all: what the trace fills is as much of the texture as the widget is currently
            // wide, and the rest has to be the clear colour rather than what a wider picture left
            // there the last time this was drawn.
            if (options.mWidth < into->getWidth() || options.mHeight < into->getHeight())
            {
                const VkClearColorValue clear{ .float32
                    = { options.mClear[0], options.mClear[1], options.mClear[2], options.mClear[3] } };
                const VkImageSubresourceRange whole{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                vkCmdClearColorImage(
                    commands, into->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &whole);

                // Both are transfer writes to the same image and nothing orders two of those.
                into->transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_PIPELINE_STAGE_2_CLEAR_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT,
                    VK_ACCESS_2_TRANSFER_WRITE_BIT);
            }

            const VkImageCopy region{
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .extent = { options.mWidth, options.mHeight, 1 },
            };
            vkCmdCopyImage(commands, mViewTarget->getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, into->getHandle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

            into->transition(commands, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
    }

    void VulkanRenderer::readGuiTexture(std::uint32_t texture, std::vector<std::uint8_t>& pixels)
    {
        mGuiTextures.read(texture, pixels);
    }

    void VulkanRenderer::readPixels(std::vector<std::uint8_t>& pixels)
    {
        assert(mTarget != nullptr);
        mTarget->read(mPool, VK_IMAGE_LAYOUT_GENERAL, pixels);
    }

    void VulkanRenderer::readChannel(Channel channel, std::vector<float>& values)
    {
        assert(mChannels != nullptr);

        const Image& image = channel == Channel::Motion ? mChannels->getMotion() : mChannels->getDepth();

        std::vector<std::uint8_t> bytes;
        image.read(mPool, VK_IMAGE_LAYOUT_GENERAL, bytes);

        values.resize(bytes.size() / sizeof(float));
        std::memcpy(values.data(), bytes.data(), bytes.size());
    }

    void VulkanRenderer::takeValidationErrors(std::vector<std::string>& errors)
    {
        errors.clear();

        ValidationLog* log = mInstance.getValidationLog();
        if (log == nullptr)
            return;

        for (const ValidationMessage& message : log->getErrorsOnThisThread())
            errors.push_back(message.mText);

        log->clear();
    }

    std::unique_ptr<Renderer> createVulkanRenderer(const RendererOptions& options, std::string& reason)
    {
        // **Where this backend's exceptions stop.** Everything below throws `Error` on a machine that
        // cannot run it — no loader, no driver, a device that does not qualify — and every caller
        // wants that as an answer rather than as an unwind.
        try
        {
            return std::make_unique<VulkanRenderer>(options);
        }
        catch (const Error& error)
        {
            reason = error.what();
            return nullptr;
        }
    }
}
