#include "dlssupscaler.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/platform/dlss.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/dlss.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/ngx.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/dlss.h>
#include <components/rtxvulkan/trace/framestep.hpp>
#include <components/version/version.hpp>

namespace Rtx
{
    namespace Ngx = Dlss::Ngx;

    namespace
    {
        /// What the input resolve binds, in the shader's own numbering. The surface and motion
        /// channels are storage images: the trace left them in `GENERAL`, and a resolve only ever
        /// loads texels.
        constexpr std::array sInputBindings{
            computeBinding(Shaders::DLSS_INPUTS_BIND_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::DLSS_INPUTS_BIND_MOTION, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::DLSS_INPUTS_BIND_DEPTH, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::DLSS_INPUTS_BIND_MOTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::DLSS_INPUTS_BIND_CONSTANTS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
        };

        constexpr ImageUse sInputWrite{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };

        /// The layout NGX is documented to read its inputs in, and the one it leaves its output in
        /// (the programming guide's 3.4). **Every stage and every access**: the evaluation records
        /// dispatches, clears and copies of its own with no barrier ahead of them, so the barriers
        /// either side of it stand for whatever it does, a `vkCmdClearColorImage` of the output among
        /// it, as the validation layers' synchronization check found on 310.9.16.
        constexpr ImageUse sDlssRead{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT };
        constexpr ImageUse sDlssWrite{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT };

        /// Motion projects the surface at both ends without jitter: a stationary surface has zero
        /// motion even as the trace samples a different subpixel, so the vectors are not marked
        /// jittered, which would have NGX subtract a jitter delta that is not there. Depth is
        /// reversed and colour is linear HDR; exposure is DLSS's own meter, because the renderer's
        /// meter runs after the upscaler.
        constexpr int sCreateFlags = Ngx::sDlssFlagIsHdr | Ngx::sDlssFlagMotionVectorsLowRes
            | Ngx::sDlssFlagDepthInverted | Ngx::sDlssFlagAutoExposure;

        /// The extent of the feature the constructor makes to learn whether DLSS runs at all.
        constexpr std::uint32_t sProbeSide = 128;

        /// The quality value NGX picks its default preset and memory budget from. The renderer's
        /// mode is the fact; reading it back from the traced extent would be a second route to the
        /// same answer, and a truncated extent can only make it less exact.
        Ngx::Quality qualityOf(const Upscale mode)
        {
            switch (mode)
            {
                case Upscale::UltraPerformance:
                    return Ngx::Quality::UltraPerformance;
                case Upscale::Performance:
                    return Ngx::Quality::MaxPerformance;
                case Upscale::Balanced:
                    return Ngx::Quality::Balanced;
                case Upscale::Quality:
                    return Ngx::Quality::MaxQuality;
                case Upscale::Native:
                    return Ngx::Quality::Dlaa;
                case Upscale::Off:
                    break;
            }

            assert(false && "a DLSS feature created with no upscale mode");
            return Ngx::Quality::MaxQuality;
        }

        std::string resultText(const Ngx::Result result)
        {
            return std::format("NGX result {:#x}", static_cast<std::uint32_t>(result));
        }

        /// What a refusal that a missing or unusable feature library can cause tells the player.
        std::string installHint()
        {
            return std::format("install the DLSS runtime from the launcher, or put {} beside the game",
                Platform::Dlss::download().mFileName);
        }

        ImageDescription storageImage(const VkExtent2D extent, const VkFormat format)
        {
            return ImageDescription{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFormat = format,
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
        }

        /// The output, which NGX also clears with `vkCmdClearColorImage` inside its evaluation, past
        /// the storage the programming guide names (3.4), as the validation layers caught on 310.9.16.
        ImageDescription outputImage(const VkExtent2D extent)
        {
            ImageDescription description = storageImage(extent, toVulkanFormat(DLSS_OUTPUT_FORMAT));
            description.mUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            return description;
        }

        Ngx::Resource viewOf(const Image& image, const bool readWrite)
        {
            Ngx::Resource resource{};
            resource.Content.ImageView.ImageView = image.getView();
            resource.Content.ImageView.Image = image.getHandle();
            resource.Content.ImageView.SubresourceRange
                = VkImageSubresourceRange{ .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                      .baseMipLevel = 0,
                      .levelCount = 1,
                      .baseArrayLayer = 0,
                      .layerCount = 1 };
            resource.Content.ImageView.Format = image.getFormat();
            resource.Content.ImageView.Width = image.getWidth();
            resource.Content.ImageView.Height = image.getHeight();
            resource.Type = Ngx::ResourceKind::ImageView;
            resource.ReadWrite = readWrite;
            return resource;
        }

        /// NGX opened for one device, and shut down for it with this.
        class Session
        {
        public:
            Session(const Ngx::Api& api, const Device& device, const Dlss::Application& application)
                : mApi(api)
                , mDevice(device.getHandle())
            {
                const Ngx::Result init = api.InitWithProjectId(Dlss::sProjectId, Ngx::Engine::Custom,
                    Version::getVersion().data(), application.getDataPath(), device.getInstanceHandle(),
                    device.getPhysicalDevice().getHandle(), mDevice, vkGetInstanceProcAddr, vkGetDeviceProcAddr,
                    Ngx::sApiVersion, application.getCommonInfo());
                if (Ngx::failed(init))
                    throw Unsupported(
                        "NVIDIA DLSS could not initialize NGX: " + resultText(init) + "; " + installHint());
            }

            ~Session()
            {
                int remaining = 0;
                mApi.Shutdown(mDevice, &remaining);
            }

            Session(const Session&) = delete;
            Session& operator=(const Session&) = delete;

        private:
            const Ngx::Api& mApi;
            VkDevice mDevice;
        };

        /// A parameter map NGX hands out, destroyed with this: `make` is the call that allocates it,
        /// `GetCapabilityParameters` or `AllocateParameters`.
        class ParameterMap
        {
        public:
            ParameterMap(const Ngx::Api& api, Ngx::Result (*make)(Ngx::Parameter**), std::string_view what)
                : mApi(api)
            {
                const Ngx::Result made = make(&mMap);
                if (Ngx::failed(made) || mMap == nullptr)
                    throw Unsupported(std::format("NVIDIA DLSS could not make NGX's {}: {}", what, resultText(made)));
            }

            ~ParameterMap() { mApi.DestroyParameters(mMap); }

            ParameterMap(const ParameterMap&) = delete;
            ParameterMap& operator=(const ParameterMap&) = delete;

            Ngx::Parameter& get() const { return *mMap; }

        private:
            const Ngx::Api& mApi;
            Ngx::Parameter* mMap = nullptr;
        };

        /// One DLSS feature, created on the device's pool as NGX's Vulkan integration asks, and
        /// released with this. Creation loads the player's feature library, so a missing or unusable
        /// one is the refusal this throws.
        class Feature
        {
        public:
            Feature(const Ngx::Api& api, const Device& device, Ngx::Parameter& parameters,
                const Ngx::DlssCreateParams& create)
                : mApi(api)
            {
                Ngx::Result result = Ngx::Result::Failure;
                device.getPool().submitAndWait([&](const VkCommandBuffer commands) {
                    result = Ngx::createDlss(api, commands, parameters, create, &mHandle);
                });
                if (Ngx::succeeded(result) && mHandle != nullptr)
                    return;

                if (mHandle != nullptr)
                    api.ReleaseFeature(mHandle);
                throw Unsupported(std::format("NVIDIA DLSS could not create its feature at {}x{} to {}x{}: {}; {}",
                    create.Feature.Width, create.Feature.Height, create.Feature.TargetWidth,
                    create.Feature.TargetHeight, resultText(result), installHint()));
            }

            ~Feature() { mApi.ReleaseFeature(mHandle); }

            Feature(const Feature&) = delete;
            Feature& operator=(const Feature&) = delete;

            const Ngx::Handle* get() const { return mHandle; }

        private:
            const Ngx::Api& mApi;
            Ngx::Handle* mHandle = nullptr;
        };

        Ngx::DlssCreateParams createParams(const Ngx::Quality quality, const VkExtent2D render, const VkExtent2D output)
        {
            return Ngx::DlssCreateParams{
                .Feature = Ngx::FeatureCreateParams{ .Width = render.width,
                    .Height = render.height,
                    .TargetWidth = output.width,
                    .TargetHeight = output.height,
                    .PerfQuality = quality },
                .Flags = sCreateFlags,
                .OutputSubrects = false,
            };
        }
    }

    /// NGX's state for one device, made in the DLSS upscaler's constructor. A configuration that
    /// only runs FSR never constructs one, so it never loads the runtime. Destroyed in reverse, so
    /// the parameter map goes before the session that handed it out.
    struct DlssUpscaler::Context
    {
        Context(const Ngx::Api& api, const Device& device, const Dlss::Application& application)
            : mApi(api)
            , mSession(api, device, application)
            , mParameters(api, api.AllocateParameters, "parameter map")
            , mInputs(device, sInputBindings, {}, "dlssinputs.comp.spv", "dlss inputs")
            , mBlocks([&](const FrameSlot) {
                return Buffer::hostWritten(
                    device, sizeof(Shaders::DlssInputConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "dlss constants");
            })
        {
            // **Capabilities from a map of their own, destroyed once read**, and the features from an
            // allocated one, as the programming guide (5.2.6) has them: a map NGX fills with every
            // capability is not one to carry through every frame's evaluation.
            {
                const ParameterMap capabilities(api, api.GetCapabilityParameters, "capabilities");
                int available = 0;
                if (const Ngx::Result queried = capabilities.get().Get(Ngx::sSuperSamplingAvailable, &available);
                    Ngx::failed(queried) || available == 0)
                    throw Unsupported("NVIDIA DLSS is not available on this card or driver; " + installHint());
            }

            // The memory a released feature holds is cached by default for the next creation. A
            // resize is frequent enough that the cache is not worth the memory a card's whole frame
            // budget would rather keep for the world.
            mParameters.get().Set(Ngx::sFreeMemoryOnRelease, 1);

            // **A small feature is the real answer before `auto` commits**, as the query before a
            // device exists can be inconclusive, and creating it loads the player's library. The
            // frame's own feature is made at its extents in `resize`; this one is released at once.
            const VkExtent2D probe{ sProbeSide, sProbeSide };
            if (false)
                const Feature probed(api, device, mParameters.get(), createParams(Ngx::Quality::Dlaa, probe, probe));
        }

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        const Ngx::Api& mApi;
        Session mSession;
        ParameterMap mParameters;
        ComputePipeline<NoConstants> mInputs;
        PerSlot<Buffer> mBlocks;
    };

    struct DlssUpscaler::Targets
    {
        Targets(const Context& context, const Device& device, const Upscale mode, const VkExtent2D render,
            const VkExtent2D output, const ArenaRun& shared)
            : mRender(render)
            , mOutput(output)
            , mShared(shared)
            , mDepth(MemoryUse::Frame, device, storageImage(render, toVulkanFormat(DLSS_DEPTH_FORMAT)), "dlss-depth")
            , mMotion(MemoryUse::Frame, device, storageImage(render, toVulkanFormat(DLSS_MOTION_FORMAT)), "dlss-motion")
            , mFeature(context.mApi, device, context.mParameters.get(), createParams(qualityOf(mode), render, output))
        {
        }

        VkExtent2D mRender;
        VkExtent2D mOutput;
        ArenaRun mShared;
        Image mDepth;
        Image mMotion;
        Feature mFeature;
    };

    std::unique_ptr<DlssUpscaler::Context> DlssUpscaler::makeContext(
        const Device& device, const Dlss::Application* application)
    {
        // **An NVIDIA card before NGX is opened**: a core the driver left in its DriverStore after
        // its card was taken out loads on any machine, and another vendor's device is not one to
        // hand it. Both asked before the context's pipeline and buffers are made, so a machine that
        // refuses pays for neither.
        if (!device.isDlssCandidate())
            throw Unsupported(std::format("NVIDIA DLSS needs an NVIDIA card, and the renderer runs on {}",
                device.getPhysicalDevice().getProperties().mProperties2.properties.deviceName));
        assert(application != nullptr && "a DLSS candidate in a run that never asked for DLSS");

        const Ngx::Api* const api = Ngx::api();
        if (api == nullptr)
            throw Unsupported("NVIDIA DLSS needs the driver's NGX core, which this machine does not have");
        return std::make_unique<Context>(*api, device, *application);
    }

    DlssUpscaler::DlssUpscaler(const Device& device, const Dlss::Application* application)
        : mDevice(device)
        , mContext(makeContext(device, application))
    {
    }

    DlssUpscaler::~DlssUpscaler() = default;

    void DlssUpscaler::declareShared(const VkExtent2D, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        into.push_back(ArenaImage{ .mDescription = outputImage(output),
            .mSpan = spanOf(FrameStep::Upscale, FrameStep::Display),
            .mName = "dlss-output" });
    }

    std::size_t DlssUpscaler::getSharedCount()
    {
        return 1;
    }

    VkDeviceSize DlssUpscaler::bytesAt(const Device& device, const VkExtent2D render, const VkExtent2D)
    {
        return Image::bytesFor(device, storageImage(render, toVulkanFormat(DLSS_DEPTH_FORMAT)))
            + Image::bytesFor(device, storageImage(render, toVulkanFormat(DLSS_MOTION_FORMAT)));
    }

    void DlssUpscaler::resize(
        const Upscale mode, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        assert(render.width > 0 && render.height > 0 && output.width >= render.width && output.height >= render.height);
        assert(shared.size() == 1 && "the DLSS upscaler's shared image was not placed as it declared it");

        if (isAt(render, output))
        {
            mTargets->mShared = shared;
            return;
        }

        // Made whole before it takes the last one's place, so a feature NGX refuses leaves nothing
        // half built behind. `FrameTracer` has let the last go already where the extents moved.
        mTargets = std::make_unique<Targets>(*mContext, mDevice, mode, render, output, shared);
        mFresh = true;
    }

    void DlssUpscaler::release()
    {
        mTargets.reset();
    }

    bool DlssUpscaler::isAt(const VkExtent2D render, const VkExtent2D output) const
    {
        const auto same
            = [](const VkExtent2D a, const VkExtent2D b) { return a.width == b.width && a.height == b.height; };
        return mTargets != nullptr && same(mTargets->mRender, render) && same(mTargets->mOutput, output);
    }

    HandedImage DlssUpscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        const GpuZone timed(timer, commands, FrameZone::Upscale);

        assert(mTargets != nullptr && "a DLSS upscale before a resize");
        Targets& targets = *mTargets;

        const bool reset = std::exchange(mFresh, false);
        const Shaders::DlssInputConstants constants{
            .mEyes = inputs.mEyes,
            .mNear = sUpscaleNear,
            .mFrame = inputs.mFrame,
        };
        const Buffer& block = mContext->mBlocks.at(inputs.mSlot);
        block.writeAt(0, std::span(&constants, 1));
        block.nameForNext();

        // **The resolve writes every pixel of both images**, so their previous contents are
        // discarded: the mode's own edge is the first frame after a resize, and the feature is new
        // then too.
        {
            Barriers barriers(commands);
            targets.mDepth.addTransition(barriers, Use::sUndefined, sInputWrite);
            targets.mMotion.addTransition(barriers, Use::sUndefined, sInputWrite);
            targets.mShared[0].addTransition(barriers, targets.mShared.firstUseOf(0), sDlssWrite);
            barriers.flush();
        }

        DescriptorWrites writes(mContext->mInputs);
        writes.image(Shaders::DLSS_INPUTS_BIND_SURFACE, inputs.mSurface.describeStorage());
        writes.image(Shaders::DLSS_INPUTS_BIND_MOTION, inputs.mMotion.describeStorage());
        writes.image(Shaders::DLSS_INPUTS_BIND_DEPTH, targets.mDepth.describeStorage());
        writes.image(Shaders::DLSS_INPUTS_BIND_MOTION_OUT, targets.mMotion.describeStorage());
        writes.buffer(Shaders::DLSS_INPUTS_BIND_CONSTANTS, block.describe(0, sizeof(constants)));
        dispatch(commands, mContext->mInputs, writes, NoConstants{},
            Groups::covering(targets.mRender.width, targets.mRender.height, Shaders::DLSS_INPUT_WORKGROUP));

        // The resolve's writes and the trace's colour handed to NGX, in one dependency. What NGX
        // does inside its evaluation is its own; the layout it is promised is the one the wrapper
        // names, and the evaluation leaves the output in `GENERAL`.
        {
            Barriers barriers(commands);
            inputs.mColour.mImage.addTransition(barriers, inputs.mColour.mLeftAs, sDlssRead);
            targets.mDepth.addTransition(barriers, sInputWrite, sDlssRead);
            targets.mMotion.addTransition(barriers, sInputWrite, sDlssRead);
            barriers.flush();
        }

        Ngx::Resource colour = viewOf(inputs.mColour.mImage, false);
        Ngx::Resource depth = viewOf(targets.mDepth, false);
        Ngx::Resource motion = viewOf(targets.mMotion, false);
        Ngx::Resource output = viewOf(targets.mShared[0], true);

        Ngx::DlssEvalParams evaluation{};
        evaluation.Feature.Color = &colour;
        evaluation.Feature.Output = &output;
        evaluation.Feature.Sharpness = 0.0f;
        evaluation.Depth = &depth;
        evaluation.MotionVectors = &motion;
        // The trace offsets the sample, whereas NGX names the resulting image displacement, which
        // is opposite. `FsrFrame` uses the same conversion for the same ray samples.
        evaluation.JitterX = -inputs.mEyes.mWorld.mJitter.x();
        evaluation.JitterY = -inputs.mEyes.mWorld.mJitter.y();
        evaluation.RenderSubrectDimensions
            = Ngx::Dimensions{ .Width = targets.mRender.width, .Height = targets.mRender.height };
        evaluation.Reset = reset ? 1 : 0;
        // The trace writes motion in render pixels, which is the unit DLSS asks for, and its
        // fourth component is the water split flag, which the resolve drops.
        evaluation.MotionVectorScaleX = 1.0f;
        evaluation.MotionVectorScaleY = 1.0f;
        evaluation.PreExposure = 1.0f;
        evaluation.ExposureScale = 1.0f;
        evaluation.FrameTimeDeltaInMsec = inputs.mSeconds * 1000.0f;

        const Ngx::Result result = Ngx::evaluateDlss(
            mContext->mApi, commands, targets.mFeature.get(), mContext->mParameters.get(), evaluation);
        if (Ngx::failed(result))
            throw DeviceError("NVIDIA DLSS failed to evaluate a frame: " + resultText(result));

        return HandedImage{ .mImage = targets.mShared[0], .mLeftAs = sDlssWrite };
    }

    std::string_view DlssUpscaler::describe()
    {
        return "NVIDIA DLSS";
    }
}
