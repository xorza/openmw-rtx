#include "dlssupscaler.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <components/debug/debuglog.hpp>
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
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/device/ngx.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/dlss.h>
#include <components/rtxvulkan/trace/framestep.hpp>
#include <components/version/version.hpp>

#include "fsrframe.hpp"

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

        /// The layout NGX is documented to read its inputs in, and the one it leaves its output in.
        /// The evaluation may change either while it runs and promises to put them back.
        constexpr ImageUse sDlssRead{ VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT };
        constexpr ImageUse sDlssWrite{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT };

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

        ImageDescription depthDescription(const VkExtent2D extent)
        {
            return ImageDescription{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFormat = toVulkanFormat(DLSS_DEPTH_FORMAT),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
        }

        ImageDescription motionDescription(const VkExtent2D extent)
        {
            return ImageDescription{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFormat = toVulkanFormat(DLSS_MOTION_FORMAT),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
        }

        ImageDescription outputDescription(const VkExtent2D extent)
        {
            return ImageDescription{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFormat = toVulkanFormat(DLSS_OUTPUT_FORMAT),
                .mUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT };
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
    }

    /// NGX's process-wide state for one device, made in the DLSS upscaler's constructor. A
    /// configuration that only runs FSR never constructs one, so it never loads the runtime.
    struct DlssUpscaler::Context
    {
        explicit Context(const Device& device)
            : mDevice(device)
            , mInputs(device, sInputBindings, {}, "dlssinputs.comp.spv", "dlss inputs")
            , mBlocks([&](const FrameSlot) {
                return Buffer::hostWritten(
                    device, sizeof(Shaders::DlssInputConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "dlss constants");
            })
        {
            mApi = Ngx::api();
            assert(mApi != nullptr && "the DLSS upscaler was made without the driver's NGX core");

            const std::wstring path = Platform::Dlss::applicationDataDirectory().wstring();
            const Ngx::Result init = mApi->InitWithProjectId(Dlss::sProjectId, Ngx::Engine::Custom,
                Version::getVersion().data(), path.c_str(), device.getInstanceHandle(),
                device.getPhysicalDevice().getHandle(), device.getHandle(), vkGetInstanceProcAddr, vkGetDeviceProcAddr,
                Ngx::sApiVersion, Dlss::featureInfo());
            if (Ngx::failed(init))
                throw Unsupported("NVIDIA DLSS could not initialize NGX: " + resultText(init)
                    + "; install the DLSS runtime from the launcher, or put nvngx_dlss.dll (Windows) / "
                      "libnvidia-ngx-dlss.so.* (Linux) beside the game");

            const Ngx::Result allocated = mApi->GetCapabilityParameters(&mParameters);
            if (Ngx::failed(allocated) || mParameters == nullptr)
            {
                int closed = 0;
                mApi->Shutdown(device.getHandle(), &closed);
                throw Unsupported("NVIDIA DLSS could not read NGX's capabilities: " + resultText(allocated));
            }

            int available = 0;
            const Ngx::Result queried = mParameters->Get(Ngx::sSuperSamplingAvailable, &available);
            if (Ngx::failed(queried) || available == 0)
            {
                mApi->DestroyParameters(mParameters);
                mParameters = nullptr;
                int closed = 0;
                mApi->Shutdown(device.getHandle(), &closed);
                throw Unsupported(
                    "NVIDIA DLSS is not available on this card or driver; install the DLSS runtime from the "
                    "launcher, or put nvngx_dlss.dll (Windows) / libnvidia-ngx-dlss.so.* (Linux) beside the game");
            }

            // The memory a released feature holds is cached by default for the next creation.
            // A resize is frequent enough that the cache is not worth the memory a card's whole
            // frame budget would rather keep for the world.
            mParameters->Set(Ngx::sFreeMemoryOnRelease, 1);

            // **A tiny feature is the real answer before `auto` commits**, as the query before a
            // device exists can be inconclusive. It also loads the player's feature library, so a
            // missing or unusable one is a refusal here, inside the constructor `auto` catches,
            // rather than a fatal error at the first resize. The frame's own feature is still made
            // at its extents; this one is released at once.
            Ngx::DlssCreateParams probe{};
            probe.Feature.Width = 128;
            probe.Feature.Height = 128;
            probe.Feature.TargetWidth = 128;
            probe.Feature.TargetHeight = 128;
            probe.Feature.PerfQuality = Ngx::Quality::Dlaa;
            probe.Flags = Ngx::sDlssFlagIsHdr | Ngx::sDlssFlagMotionVectorsLowRes | Ngx::sDlssFlagDepthInverted
                | Ngx::sDlssFlagAutoExposure;
            probe.OutputSubrects = false;

            Ngx::Handle* probeFeature = nullptr;
            Ngx::Result probed = Ngx::Result::Failure;
            mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) {
                probed = Ngx::createDlss(commands, mParameters, probe, &probeFeature);
            });
            if (Ngx::failed(probed) || probeFeature == nullptr)
            {
                if (probeFeature != nullptr)
                    mApi->ReleaseFeature(probeFeature);
                mApi->DestroyParameters(mParameters);
                mParameters = nullptr;
                int closed = 0;
                mApi->Shutdown(device.getHandle(), &closed);
                throw Unsupported("NVIDIA DLSS could not create a feature: " + resultText(probed)
                    + "; install the DLSS runtime from the launcher, or put nvngx_dlss.dll (Windows) / "
                      "libnvidia-ngx-dlss.so.* (Linux) beside the game");
            }
            mApi->ReleaseFeature(probeFeature);
        }

        ~Context()
        {
            if (mParameters != nullptr)
                mApi->DestroyParameters(mParameters);
            int closed = 0;
            mApi->Shutdown(mDevice.getHandle(), &closed);
        }

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        const Device& mDevice;
        ComputePipeline<NoConstants> mInputs;
        PerSlot<Buffer> mBlocks;
        const Ngx::Api* mApi = nullptr;
        Ngx::Parameter* mParameters = nullptr;
    };

    struct DlssUpscaler::Targets
    {
        Targets(const Device& device, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
            : mRender(render)
            , mOutput(output)
            , mShared(shared)
            , mDepth(MemoryUse::Frame, device, depthDescription(render), "dlss-depth")
            , mMotion(MemoryUse::Frame, device, motionDescription(render), "dlss-motion")
        {
        }

        ~Targets()
        {
            if (mFeature != nullptr)
                Ngx::api()->ReleaseFeature(mFeature);
        }

        Targets(const Targets&) = delete;
        Targets& operator=(const Targets&) = delete;

        VkExtent2D mRender;
        VkExtent2D mOutput;
        ArenaRun mShared;
        Image mDepth;
        Image mMotion;
        Ngx::Handle* mFeature = nullptr;
    };

    std::unique_ptr<DlssUpscaler::Context> DlssUpscaler::makeContext(const Device& device)
    {
        // Checked before the context's pipeline and buffers are made: a machine without the
        // driver's core should not pay for either before the refusal.
        if (Ngx::api() == nullptr)
            throw Unsupported("NVIDIA DLSS needs the driver's NGX core, which this machine does not have");
        return std::make_unique<Context>(device);
    }

    DlssUpscaler::DlssUpscaler(const Device& device)
        : mDevice(device)
        , mContext(makeContext(device))
    {
    }

    DlssUpscaler::~DlssUpscaler() = default;

    void DlssUpscaler::declareShared(const VkExtent2D, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        into.push_back(ArenaImage{ .mDescription = outputDescription(output),
            .mSpan = spanOf(FrameStep::Upscale, FrameStep::Display),
            .mName = "dlss-output" });
    }

    std::size_t DlssUpscaler::getSharedCount()
    {
        return 1;
    }

    VkDeviceSize DlssUpscaler::bytesAt(const Device& device, const VkExtent2D render, const VkExtent2D)
    {
        return Image::bytesFor(device, depthDescription(render)) + Image::bytesFor(device, motionDescription(render));
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

        // **The feature is made after the device and with a command buffer**, as NGX's Vulkan
        // integration asks: creation records its own work, and a resize is already a drained point.
        mTargets = std::make_unique<Targets>(mDevice, render, output, shared);

        Ngx::DlssCreateParams create{};
        create.Feature.Width = render.width;
        create.Feature.Height = render.height;
        create.Feature.TargetWidth = output.width;
        create.Feature.TargetHeight = output.height;
        create.Feature.PerfQuality = qualityOf(mode);

        // Motion projects the surface at both ends without jitter: a stationary surface has zero
        // motion even as the trace samples a different subpixel. Marking it jittered makes NGX
        // subtract a jitter delta that is not there. Depth is reversed and colour is linear HDR;
        // exposure is DLSS's own meter, because the renderer's meter runs after the upscaler.
        create.Flags = Ngx::sDlssFlagIsHdr | Ngx::sDlssFlagMotionVectorsLowRes | Ngx::sDlssFlagDepthInverted
            | Ngx::sDlssFlagAutoExposure;
        create.OutputSubrects = false;

        Ngx::Result result = Ngx::Result::Failure;
        mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) {
            result = Ngx::createDlss(commands, mContext->mParameters, create, &mTargets->mFeature);
        });

        if (Ngx::failed(result) || mTargets->mFeature == nullptr)
            throw Unsupported("NVIDIA DLSS could not create its feature at " + std::to_string(render.width) + "x"
                + std::to_string(render.height) + " to " + std::to_string(output.width) + "x"
                + std::to_string(output.height) + ": " + resultText(result));

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
        assert(mContext != nullptr && "a DLSS upscale without NGX");
        Targets& targets = *mTargets;

        const bool reset = std::exchange(mFresh, false);
        const Shaders::DlssInputConstants constants{
            .mEyes = inputs.mEyes,
            .mNear = FsrFrame::sNear,
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

        // The resolve's writes and the trace's channels handed to NGX, in one dependency. What NGX
        // does inside its evaluation is its own; the layout it is promised is the one the wrapper
        // names, and the evaluation leaves the output in `GENERAL`.
        {
            Barriers barriers(commands);
            inputs.mColour.addTransition(barriers, Use::sAnyGeneralRead, sDlssRead);
            targets.mDepth.addTransition(barriers, sInputWrite, sDlssRead);
            targets.mMotion.addTransition(barriers, sInputWrite, sDlssRead);
            barriers.flush();
        }

        Ngx::Resource colour = viewOf(inputs.mColour, false);
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

        const Ngx::Result result = Ngx::evaluateDlss(commands, targets.mFeature, mContext->mParameters, evaluation);
        if (Ngx::failed(result))
            throw std::runtime_error("NVIDIA DLSS failed to evaluate a frame: " + resultText(result));

        return HandedImage{ .mImage = targets.mShared[0], .mLeftAs = Use::sComputeReadWrite };
    }

    std::string_view DlssUpscaler::describe()
    {
        return "NVIDIA DLSS";
    }
}
