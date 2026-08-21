#include "vulkanrenderer.hpp"

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

    VulkanRenderer::VulkanRenderer(const RendererOptions& options)
        : mInstance(instanceOptionsFor(options))
        , mDevice(mInstance, PhysicalDevice::select(mInstance.getHandle()), deviceExtensionsFor(options))
        , mPool(mDevice)
        , mShaderDirectory(options.mShaderDirectory)
        , mUpscale(options.mUpscale)
        , mFilter(mDevice, options.mShaderDirectory)
        , mComposite(mDevice, mPool, options.mShaderDirectory)
        , mExposure(mDevice, options.mShaderDirectory)
        , mTone(mDevice, options.mShaderDirectory)
    {
        mHitCount = Buffer(mDevice, sizeof(std::uint32_t),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        // Before the first targets, because what to trace at is its answer and not ours.
        if (mUpscale != Upscale::Off)
        {
#ifdef OPENMW_RTX_DLSS
            mNgx = std::make_unique<Dlss>(mDevice, mInstance.getHandle());
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
        mTarget = std::make_unique<Image>(mDevice, mOutputWidth, mOutputHeight, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, "target", Sharing::Exportable);

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
            const Dlss ngx(mDevice, mInstance.getHandle());
            report += ngx.isAvailable() ? "available\n" : "unavailable, " + ngx.getObstacle() + "\n";
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

        mAcceleration = std::make_unique<SceneAcceleration>(mDevice, mPool, scene);
        mBuffers = std::make_unique<SceneBuffers>(mDevice, mPool, scene, mAcceleration->getIndices(), sea);
        mTextures = std::make_unique<TextureArray>(mDevice, mPool, textures);

        // **Built once and kept, because building one compiles the shader — half a second a time,
        // measured.** Nothing about the pass depends on the scene: it needs the device and the shape
        // of the texture set, and every array declares that shape identically — the bindless binding
        // is sized to its maximum rather than to the cell, so what varies between scenes is how many
        // descriptors get allocated and never what the layout says. Identically defined layouts are
        // compatible, so a set from a later array binds against the pipeline layout the first one
        // produced. `TextureArray`'s layout is where that invariant is kept.
        if (mPass == nullptr)
            mPass = std::make_unique<VisibilityPass>(mDevice, mPool, mShaderDirectory, mTextures->getLayout());

        mStats = SceneStats{
            .mInstances = mAcceleration->getInstanceCount(),
            .mCutoutInstances = mAcceleration->getCutoutInstanceCount(),
            .mStructureBytes = mAcceleration->getStructureBytes(),
            .mTableBytes = mBuffers->getBytes(),
            .mTextureCount = mTextures->getCount(),
            .mTextureBytes = mTextures->getBytes(),
        };
    }

    void VulkanRenderer::placeScene(const SceneDesc& scene, const SeaState& sea)
    {
        assert(mAcceleration != nullptr && "placeScene before setScene");

        mAcceleration->placeInstances(mPool, scene);

        // **Rebuilt whole rather than in part, and measured before it is split.** Most of what this
        // uploads has not changed — the vertices, the meshes, the materials — and only the instance
        // records, the lights and the grid over them have. Splitting it is an obvious optimisation
        // and an unmeasured one; a cell's tables are a few megabytes, which is a cost worth seeing
        // in a profile before it is designed around.
        mBuffers = std::make_unique<SceneBuffers>(mDevice, mPool, scene, mAcceleration->getIndices(), sea);

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
            .mTextures = mTextures->getSet(),
            .mShading = mTextures->getShading(),
        };

        // Made by the first frame that averages, and that frame is the one that fills it.
        const bool fresh = options.mAccumulate > 0 && mHistory == nullptr;
        if (fresh)
            mHistory = std::make_unique<Image>(mDevice, mRenderWidth, mRenderHeight, VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT, "history");

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
            mPass->record(commands, inputs, *mChannels, mHitCount, sampled);
            mChannels->handOver(commands);

            // Where the bounce ended up: the filter's last level, or the channel the trace wrote
            // when nothing filtered it. **Ray Reconstruction is itself the denoiser**, and handing
            // it a frame the wavelet already blurred is asking it to recover what was thrown away.
            const Image& indirect = options.mFilter && !upscaling ? mFilter.record(commands, *mChannels, sampled)
                                                                  : mChannels->getIndirect();

            mComposite.record(commands, *mChannels, indirect, mHistory.get(), *mColour,
                Shaders::CompositeConstants{
                    .mWidth = mRenderWidth,
                    .mHeight = mRenderHeight,
                    .mAccumulate = options.mAccumulate,
                });

            // Whatever comes next reads what the composite just wrote.
            mColour->transition(commands, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT);

            const Image* shown = mColour.get();

#ifdef OPENMW_RTX_DLSS
            if (mUpscaler != nullptr)
            {
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

                shown = mUpscaled.get();
            }
#endif

            // **Measured off the image the curve is about to map**, which is the upscaled one
            // wherever something upscales — see `histogram.comp` for what measuring the other one
            // costs. One `shown` feeds both, so the two cannot come apart.
            if (options.mExposure.has_value())
                mExposure.recordFixed(commands, *options.mExposure);
            else
                mExposure.record(commands, *shown);

            mTone.record(commands, *shown, mExposure.getExposure(), *mTarget);
        });

        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        // What the next frame reprojects against, and the camera as the caller gave it: a jitter is
        // where inside a pixel this frame sampled, not where the eye was.
        mPreviousCamera = camera;

        const std::uint32_t hits = *static_cast<const std::uint32_t*>(mHitCount.map());
        mHitCount.unmap();

        return FrameResult{ .mHits = hits, .mTraceMs = ms };
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
