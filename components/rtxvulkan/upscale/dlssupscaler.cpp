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

#include <components/crashcatcher/crashnote.hpp>
#include <components/debug/debuglog.hpp>
#include <components/rtx/common/error.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/driverprofile.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/frame/framestep.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/dlss.h>
#include <components/rtxvulkan/shaders/shared/upscaleinputs.h>
#include <components/version/version.hpp>

#include "dlssapplication.hpp"
#include "ngx.hpp"

namespace Rtx
{
    namespace Ngx = Dlss::Ngx;

    namespace
    {
        /// What the guide pass binds, in the shader's own numbering. The channels are storage images:
        /// the trace left them in `GENERAL`, and the pass only ever loads texels.
        constexpr std::array sGuideBindings{
            computeBinding(Shaders::RR_GUIDES_BIND_SURFACE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_MOTION, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_DEPTH, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_MOTION_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_DIFFUSE_ALBEDO, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO_OUT, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_NORMAL_ROUGHNESS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::RR_GUIDES_BIND_CONSTANTS, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER),
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
        /// reversed and colour is linear HDR. No exposure flag: Ray Reconstruction supports none
        /// (DLSS-RR Integration Guide, 3.7).
        constexpr int sCreateFlags
            = Ngx::sDlssFlagIsHdr | Ngx::sDlssFlagMotionVectorsLowRes | Ngx::sDlssFlagDepthInverted;

        /// Where each image `declareShared` declares stands in the arena's run, in its order.
        constexpr std::size_t sOutput = 0;
        constexpr std::size_t sDepth = 1;
        constexpr std::size_t sMotion = 2;
        constexpr std::size_t sDiffuseAlbedo = 3;
        constexpr std::size_t sSpecularAlbedo = 4;
        constexpr std::size_t sNormalRoughness = 5;
        constexpr std::size_t sShared = 6;

        /// The guides, which the guide pass writes whole each frame and NGX reads.
        constexpr std::array sGuides{ sDepth, sMotion, sDiffuseAlbedo, sSpecularAlbedo, sNormalRoughness };

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

        /// What a refusal that a missing or unusable feature library can cause tells the player: the
        /// Ray Reconstruction library, which a player holding only Super Resolution's lacks.
        std::string installHint()
        {
            return std::format("install the DLSS runtime from the launcher, or put {} beside the game",
                DlssLibrary::download().mFileName);
        }

        /// Why the capability map says DLSS is not available, as the programming guide (5.2.6) has a
        /// caller read it: a driver too old names the version it needs, a feature that failed to start
        /// names NGX's result, and only what a missing or unusable library may cause asks for one.
        std::string refusalOf(const Ngx::Parameter& capabilities)
        {
            int needsDriver = 0;
            if (Ngx::succeeded(capabilities.Get(Ngx::sRayReconstructionNeedsUpdatedDriver, &needsDriver))
                && needsDriver != 0)
            {
                unsigned int major = 0;
                unsigned int minor = 0;
                capabilities.Get(Ngx::sRayReconstructionMinDriverMajor, &major);
                capabilities.Get(Ngx::sRayReconstructionMinDriverMinor, &minor);
                return std::format("NVIDIA DLSS needs a newer graphics driver, {}.{} or later", major, minor);
            }

            int started = 0;
            if (Ngx::succeeded(capabilities.Get(Ngx::sRayReconstructionFeatureInitResult, &started)) && started != 0
                && Ngx::failed(static_cast<Ngx::Result>(static_cast<std::uint32_t>(started))))
                return "NVIDIA DLSS could not start on this card: "
                    + resultText(static_cast<Ngx::Result>(static_cast<std::uint32_t>(started))) + "; " + installHint();

            return "NVIDIA DLSS is not available on this card or driver; " + installHint();
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
            ImageDescription description = storageImage(extent, toVulkanFormat(RR_OUTPUT_FORMAT));
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
                const Crash::NoteScope noted("initializing NVIDIA's NGX");
                const Ngx::Result init = api.InitWithProjectId(Dlss::sProjectId, Ngx::Engine::Custom,
                    Version::getVersion().data(), application.getDataPath(), device.getInstanceHandle(),
                    device.getPhysicalDevice().getHandle(), mDevice, vkGetInstanceProcAddr, vkGetDeviceProcAddr,
                    Ngx::sApiVersion, application.getCommonInfo());
                if (Ngx::failed(init))
                    throw UpscalerRefused(
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
                    throw UpscalerRefused(
                        std::format("NVIDIA DLSS could not make NGX's {}: {}", what, resultText(made)));
            }

            ~ParameterMap() { mApi.DestroyParameters(mMap); }

            ParameterMap(const ParameterMap&) = delete;
            ParameterMap& operator=(const ParameterMap&) = delete;

            Ngx::Parameter& get() const { return *mMap; }

        private:
            const Ngx::Api& mApi;
            Ngx::Parameter* mMap = nullptr;
        };

        /// One Ray Reconstruction feature, created on the device's pool as NGX's Vulkan integration
        /// asks, and released with this. Creation loads the player's feature library, so a missing or
        /// unusable one is the refusal this throws.
        class Feature
        {
        public:
            Feature(const Ngx::Api& api, const Device& device, Ngx::Parameter& parameters,
                const Ngx::RayReconstructionFeature& create)
                : mApi(api)
            {
                const Crash::NoteScope noted("creating NVIDIA DLSS's feature at {}x{} to {}x{}", create.mRender.width,
                    create.mRender.height, create.mOutput.width, create.mOutput.height);
                Ngx::Result result = Ngx::Result::Failure;
                device.getPool().submitAndWait([&](const VkCommandBuffer commands) {
                    result = Ngx::createRayReconstruction(api, commands, parameters, create, &mHandle);
                });
                if (Ngx::succeeded(result) && mHandle != nullptr)
                    return;

                if (mHandle != nullptr)
                    api.ReleaseFeature(mHandle);
                throw UpscalerRefused(std::format("NVIDIA DLSS could not create its feature at {}x{} to {}x{}: {}; {}",
                    create.mRender.width, create.mRender.height, create.mOutput.width, create.mOutput.height,
                    resultText(result), installHint()));
            }

            ~Feature() { mApi.ReleaseFeature(mHandle); }

            Feature(const Feature&) = delete;
            Feature& operator=(const Feature&) = delete;

            const Ngx::Handle* get() const { return mHandle; }

        private:
            const Ngx::Api& mApi;
            Ngx::Handle* mHandle = nullptr;
        };

        Ngx::RayReconstructionFeature featureAt(
            const Ngx::Quality quality, const VkExtent2D render, const VkExtent2D output)
        {
            return Ngx::RayReconstructionFeature{
                .mRender = render, .mOutput = output, .mQuality = quality, .mFlags = sCreateFlags
            };
        }

        bool sameExtent(const VkExtent2D a, const VkExtent2D b)
        {
            return a.width == b.width && a.height == b.height;
        }
    }

    /// NGX's state for one device, made in the DLSS upscaler's constructor. A configuration that
    /// only runs FSR never constructs one, so it never loads the runtime. Destroyed in reverse, so
    /// the parameter maps go before the session that handed them out.
    struct DlssUpscaler::Context
    {
        Context(const Ngx::Api& api, const Device& device, const Dlss::Application& application)
            : mApi(api)
            , mSession(api, device, application)
            , mCapabilities(api, api.GetCapabilityParameters, "capabilities")
            , mParameters(api, api.AllocateParameters, "parameter map")
            , mGuides(device, sGuideBindings, {}, "rrguides.comp.spv", "rr guides")
            , mBlocks(Buffer::deviceLocal(device, sizeof(Shaders::RrGuideConstants),
                  VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "rr constants"))
        {
            // **The capabilities and the features from maps of their own**, as the programming guide
            // (5.2.6) has them: a map NGX fills with every capability is not one to carry through
            // every frame's evaluation. The capability map stays, read only where a feature is made,
            // since the optimal settings' and the stats' callbacks are published on it alone.
            int available = 0;
            if (const Ngx::Result queried = mCapabilities.get().Get(Ngx::sRayReconstructionAvailable, &available);
                Ngx::failed(queried) || available == 0)
                throw UpscalerRefused(refusalOf(mCapabilities.get()));

            // **A library that cannot say what its features hold is refused here**, where `auto` can
            // still fall back, and not at the first resize, whose reserve counts what they hold.
            unsigned long long held = 0;
            if (const Ngx::Result stated = Ngx::statsBytes(mCapabilities.get(), held); Ngx::failed(stated))
                throw UpscalerRefused("NVIDIA DLSS's library does not state what its features hold: "
                    + resultText(stated) + "; " + installHint());

            // The memory a released feature holds is cached by default for the next creation. A
            // resize is frequent enough that the cache is not worth the memory a card's whole frame
            // budget would rather keep for the world.
            mParameters.get().Set(Ngx::sFreeMemoryOnRelease, 1);
            Ngx::setRayReconstructionConstants(mParameters.get());

            // **A small feature is the real answer before `auto` commits**, as the query before a
            // device exists can be inconclusive, and creating it loads the player's library. The
            // frame's own feature is made at its extents in `resize`; this one is released at once.
            const VkExtent2D probe{ sProbeSide, sProbeSide };
            const Feature probed(api, device, mParameters.get(), featureAt(Ngx::Quality::Dlaa, probe, probe));
        }

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        const Ngx::Api& mApi;
        Session mSession;
        ParameterMap mCapabilities;
        ParameterMap mParameters;
        ComputePipeline<NoConstants> mGuides;
        /// The guide pass's constants, written in queue order (`Buffer::updateInline`): one buffer
        /// for every frame.
        Buffer mBlocks;
    };

    struct DlssUpscaler::Targets
    {
        Targets(const Context& context, const Device& device, const Upscale mode, const VkExtent2D render,
            const VkExtent2D output)
            : mRender(render)
            , mOutput(output)
            , mFeature(context.mApi, device, context.mParameters.get(), featureAt(qualityOf(mode), render, output))
        {
        }

        VkExtent2D mRender;
        VkExtent2D mOutput;
        /// The output, then the guides, which live inside the upscale step alone: empty until `resize`
        /// places them.
        ArenaRun mShared;
        Feature mFeature;

        /// What NGX held for the feature as `prepare` measured it.
        VkDeviceSize mBytes = 0;
    };

    std::unique_ptr<DlssUpscaler::Context> DlssUpscaler::makeContext(
        const Device& device, const Dlss::Application* application)
    {
        // **An NVIDIA card before NGX is opened**: a core the driver left in its DriverStore after
        // its card was taken out loads on any machine, and another vendor's device is not one to
        // hand it. Both asked before the context's pipeline and buffers are made, so a machine that
        // refuses pays for neither.
        if (!device.getPhysicalDevice().getDriver().mNgx)
            throw UpscalerRefused(std::format("NVIDIA DLSS needs an NVIDIA card, and the renderer runs on {}",
                device.getPhysicalDevice().getProperties().mProperties2.properties.deviceName));
        assert(application != nullptr && "a DLSS candidate in a run that never asked for DLSS");

        const Ngx::Api* const api = Ngx::api();
        if (api == nullptr)
            throw UpscalerRefused("NVIDIA DLSS needs the driver's NGX core, which this machine does not have");
        return std::make_unique<Context>(*api, device, *application);
    }

    DlssAvailability DlssUpscaler::availabilityOn(const DriverProfile& driver, const DlssLibrary::Folders& folders)
    {
        if (!driver.mNgx)
            return DlssAvailability::NotNvidia;
        if (!DlssLibrary::installedIn(folders))
            return DlssAvailability::NotInstalled;
        return DlssAvailability::Present;
    }

    DlssUpscaler::DlssUpscaler(const Device& device, const Dlss::Application* application)
        : mDevice(device)
        , mContext([&] {
            const Crash::NoteScope noted("starting NVIDIA DLSS Ray Reconstruction");
            return makeContext(device, application);
        }())
    {
    }

    DlssUpscaler::~DlssUpscaler() = default;

    void DlssUpscaler::declareShared(const VkExtent2D render, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        into.push_back(ArenaImage{ .mDescription = outputImage(output),
            .mSpan = spanOf(FrameStep::Upscale, FrameStep::Display),
            .mName = "rr-output" });
        const auto guide = [&](const VkFormat format, const std::string_view name) {
            into.push_back(ArenaImage{ .mDescription = storageImage(render, format),
                .mSpan = spanOf(FrameStep::Upscale, FrameStep::Upscale),
                .mName = name });
        };
        guide(toVulkanFormat(RR_DEPTH_FORMAT), "rr-depth");
        guide(toVulkanFormat(RR_MOTION_FORMAT), "rr-motion");
        guide(toVulkanFormat(RR_GUIDE_FORMAT), "rr-diffuse-albedo");
        guide(toVulkanFormat(RR_GUIDE_FORMAT), "rr-specular-albedo");
        guide(toVulkanFormat(RR_GUIDE_FORMAT), "rr-normal-roughness");
    }

    std::size_t DlssUpscaler::getSharedCount()
    {
        return sShared;
    }

    VkExtent2D DlssUpscaler::renderExtentFor(const Upscale mode, const VkExtent2D output) const
    {
        // NGX rounds its own ratios (balanced at 1920 by 1080 is 1114 by 626, where 1.7 truncates to
        // 1129 by 635), which only it knows: the extent is asked, never worked out.
        Ngx::OptimalSettings settings;
        const Ngx::Result asked
            = Ngx::optimalSettings(mContext->mCapabilities.get(), output, qualityOf(mode), settings);
        if (Ngx::failed(asked) || settings.mOptimal.width == 0 || settings.mOptimal.height == 0)
            throw DeviceError(std::format("NVIDIA DLSS gave no extent to trace {} at for {}x{}: {}",
                sUpscaleNames.name(mode), output.width, output.height, resultText(asked)));
        return settings.mOptimal;
    }

    VkDeviceSize DlssUpscaler::bytesAt(const VkExtent2D render, const VkExtent2D output) const
    {
        for (const Measured& measured : mMeasured)
            if (sameExtent(measured.mRender, render) && sameExtent(measured.mOutput, output))
                return measured.mBytes;
        return 0;
    }

    VkDeviceSize DlssUpscaler::getHeldOutside() const
    {
        return mTargets != nullptr ? mTargets->mBytes : 0;
    }

    void DlssUpscaler::remember(const Measured& measured)
    {
        // At one output only: a resize of the window measures its modes again, and what was measured
        // at another output names extents no mode traces at now.
        std::erase_if(mMeasured, [&](const Measured& kept) {
            return !sameExtent(kept.mOutput, measured.mOutput) || sameExtent(kept.mRender, measured.mRender);
        });
        mMeasured.push_back(measured);
        assert(mMeasured.size() <= sUpscaleMenu.size() && "a mode measured twice at one output");
    }

    DlssUpscaler::Measured DlssUpscaler::makeAndMeasure(
        const Upscale mode, const VkExtent2D render, const VkExtent2D output, std::unique_ptr<Targets>& into)
    {
        // **The stats after the feature stands less the stats before it**, since NGX's stats are what
        // every one of its features holds: a feature standing beside this one is not this one's.
        unsigned long long before = 0;
        unsigned long long after = 0;
        Ngx::Parameter& capabilities = mContext->mCapabilities.get();
        const Ngx::Result was = Ngx::statsBytes(capabilities, before);

        // **A refusal here is the device's**: the constructor's feature proved that NGX, the driver and
        // the player's library run, and `FrameTracer` has let the last targets go already where the
        // extents moved, so nothing stands to fall back to.
        try
        {
            into = std::make_unique<Targets>(*mContext, mDevice, mode, render, output);
        }
        catch (const UpscalerRefused& refused)
        {
            throw DeviceError(refused.what());
        }

        const Ngx::Result now = Ngx::statsBytes(capabilities, after);
        if (Ngx::failed(was) || Ngx::failed(now))
            throw DeviceError("NVIDIA DLSS could not say what its feature holds: " + resultText(now));
        // At Info, once a mode's feature is made: a player's log then says what the card held for it.
        Log(Debug::Info) << std::format(
            "NVIDIA DLSS Ray Reconstruction's feature, {} at {}x{} to {}x{}: NGX holds {} bytes, {} before it",
            sUpscaleNames.name(mode), render.width, render.height, output.width, output.height, after, before);
        return Measured{ .mRender = render, .mOutput = output, .mBytes = after > before ? after - before : 0 };
    }

    void DlssUpscaler::prepare(const Upscale mode, const VkExtent2D render, const VkExtent2D output)
    {
        assert(render.width > 0 && render.height > 0 && output.width >= render.width && output.height >= render.height);
        if (isAt(render, output))
            return;

        mTargets.reset();
        const Measured measured = makeAndMeasure(mode, render, output, mTargets);
        mTargets->mBytes = measured.mBytes;
        remember(measured);
        mFresh = true;
    }

    void DlssUpscaler::measure(const Upscale mode, const VkExtent2D render, const VkExtent2D output)
    {
        for (const Measured& measured : mMeasured)
            if (sameExtent(measured.mRender, render) && sameExtent(measured.mOutput, output))
                return;

        std::unique_ptr<Targets> probe;
        remember(makeAndMeasure(mode, render, output, probe));
    }

    void DlssUpscaler::resize(const Upscale, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        assert(shared.size() == sShared && "the DLSS upscaler's shared images were not placed as it declared them");
        assert(isAt(render, output) && "a DLSS resize to extents `prepare` did not make the feature at");
        mTargets->mShared = shared;
    }

    void DlssUpscaler::release()
    {
        mTargets.reset();
    }

    bool DlssUpscaler::isAt(const VkExtent2D render, const VkExtent2D output) const
    {
        return mTargets != nullptr && sameExtent(mTargets->mRender, render) && sameExtent(mTargets->mOutput, output);
    }

    HandedImage DlssUpscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        const GpuZone timed(timer, commands, FrameZone::Upscale);

        assert(mTargets != nullptr && mTargets->mShared.size() == sShared && "a DLSS upscale before a resize");
        Targets& targets = *mTargets;

        const bool reset = std::exchange(mFresh, false);
        const Shaders::RrGuideConstants constants{
            .mInputs = Shaders::UpscaleInputConstants{
                .mEyes = inputs.mEyes,
                .mNear = sUpscaleNear,
                .mFrame = inputs.mFrame,
            },
            .mLobed = inputs.mLobed ? 1u : 0u,
        };
        const Buffer& block = mContext->mBlocks;
        block.updateInline(commands, Use::sBufferUniformRead, std::as_bytes(std::span(&constants, 1)));

        // **The guide pass writes every pixel of every guide**, so their previous contents are
        // discarded: the mode's own edge is the first frame after a resize, and the feature is new
        // then too.
        {
            Barriers barriers(commands);
            targets.mShared[sOutput].addTransition(barriers, targets.mShared.firstUseOf(sOutput), sDlssWrite);
            for (const std::size_t guide : sGuides)
                targets.mShared[guide].addTransition(barriers, targets.mShared.firstUseOf(guide), sInputWrite);
            barriers.flush();
        }

        DescriptorWrites writes(mContext->mGuides);
        writes.image(Shaders::RR_GUIDES_BIND_SURFACE, inputs.mSurface.describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_MOTION, inputs.mMotion.describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_ALBEDO, inputs.mAlbedo.describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO, inputs.mSpecularAlbedo.describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_SPECULAR, inputs.mSpecular.describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_DEPTH, targets.mShared[sDepth].describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_MOTION_OUT, targets.mShared[sMotion].describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_DIFFUSE_ALBEDO, targets.mShared[sDiffuseAlbedo].describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_SPECULAR_ALBEDO_OUT, targets.mShared[sSpecularAlbedo].describeStorage());
        writes.image(Shaders::RR_GUIDES_BIND_NORMAL_ROUGHNESS, targets.mShared[sNormalRoughness].describeStorage());
        writes.buffer(Shaders::RR_GUIDES_BIND_CONSTANTS, block.describe(0, sizeof(constants)));
        dispatch(commands, mContext->mGuides, writes, NoConstants{},
            Groups::covering(targets.mRender.width, targets.mRender.height, Shaders::RR_GUIDES_WORKGROUP));

        // The guides and the trace's colour handed to NGX, in one dependency. What NGX does inside its
        // evaluation is its own; the layout it is promised is the one the wrapper names, and the
        // evaluation leaves the output in `GENERAL`.
        {
            Barriers barriers(commands);
            inputs.mColour.mImage.addTransition(barriers, inputs.mColour.mLeftAs, sDlssRead);
            for (const std::size_t guide : sGuides)
                targets.mShared[guide].addTransition(barriers, sInputWrite, sDlssRead);
            barriers.flush();
        }

        Ngx::Resource colour = viewOf(inputs.mColour.mImage, false);
        Ngx::Resource depth = viewOf(targets.mShared[sDepth], false);
        Ngx::Resource motion = viewOf(targets.mShared[sMotion], false);
        Ngx::Resource diffuse = viewOf(targets.mShared[sDiffuseAlbedo], false);
        Ngx::Resource specular = viewOf(targets.mShared[sSpecularAlbedo], false);
        Ngx::Resource normals = viewOf(targets.mShared[sNormalRoughness], false);
        Ngx::Resource output = viewOf(targets.mShared[sOutput], true);

        // The trace offsets the sample, whereas NGX names the resulting image displacement, which is
        // opposite. `FsrFrame` uses the same conversion for the same ray samples.
        const Ngx::RayReconstructionFrame frame{ .mColour = &colour,
            .mOutput = &output,
            .mDepth = &depth,
            .mMotion = &motion,
            .mDiffuseAlbedo = &diffuse,
            .mSpecularAlbedo = &specular,
            .mNormalRoughness = &normals,
            .mJitterX = -inputs.mEyes.mWorld.mJitter.x(),
            .mJitterY = -inputs.mEyes.mWorld.mJitter.y(),
            .mRender = targets.mRender,
            .mReset = reset,
            .mFrameMs = inputs.mSeconds * 1000.0f };

        const Crash::NoteScope noted("evaluating NVIDIA DLSS at {}x{}", frame.mRender.width, frame.mRender.height);
        const Ngx::Result result = Ngx::evaluateRayReconstruction(
            mContext->mApi, commands, targets.mFeature.get(), mContext->mParameters.get(), frame);
        if (Ngx::failed(result))
            throw DeviceError(std::format("NVIDIA DLSS failed to evaluate a frame traced at {}x{}{}: {}",
                frame.mRender.width, frame.mRender.height, reset ? ", its history reset" : "", resultText(result)));

        // **The colour back where the trace handed it**, which is where a reader of the composite
        // after the frame meets it (`UpscaleInputs::mColour`): one barrier, against a second
        // answer to where the composite stands that every later reader would have to be handed.
        {
            Barriers barriers(commands);
            inputs.mColour.mImage.addTransition(barriers, sDlssRead, inputs.mColour.mLeftAs);
            barriers.flush();
        }

        return HandedImage{ .mImage = targets.mShared[sOutput], .mLeftAs = sDlssWrite };
    }

    std::string_view DlssUpscaler::describe()
    {
        return "NVIDIA DLSS Ray Reconstruction";
    }
}
