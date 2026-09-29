#include "upscaler.hpp"

#include <algorithm>
#include <cassert>
#include <span>
#include <utility>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/shaders/fsr.h>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>

namespace Rtx
{
    namespace
    {
        /// Everything a pass binds, by what it is rather than by where it is: `Targets::pick` says
        /// where, which for the paired histories is the frame's parity.
        enum class Bound : std::uint8_t
        {
            Motion,
            Surface,
            Colour,
            Puffs,
            DilatedMotion,
            DilatedDepth,
            PreviousDepth,
            Intermediate,
            LumaNow,
            LumaBefore,
            SpdAtomic,
            FrameInfo,
            SpdMips,
            FarthestDepthMip1,
            ShadingChange,
            AccumulationBefore,
            AccumulationNow,
            DilatedMasks,
            NewLocks,
            LumaHistoryBefore,
            LumaHistoryNow,
            HistoryBefore,
            HistoryNow,
            Output,
            Constants,
            Pyramid,
            Inputs,
            Sampler,
        };

        /// How a pass reads a resource: through the texture unit, as storage (`mLevel` names the
        /// level), as a constant block, or as the sampler.
        enum class Access : std::uint8_t
        {
            Sampled,
            Storage,
            Block,
            Sampler,
        };

        struct Bind
        {
            std::uint32_t mBinding;
            Access mAccess;
            Bound mBound;
            std::uint32_t mLevel = 0;
        };

        constexpr VkDescriptorType typeOf(const Access access)
        {
            switch (access)
            {
                case Access::Sampled:
                    return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
                case Access::Storage:
                    return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                case Access::Block:
                    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                case Access::Sampler:
                    break;
            }

            return VK_DESCRIPTOR_TYPE_SAMPLER;
        }

        using S = Access;
        using R = Bound;
        namespace F = Shaders;

        // Each pass's bindings, as its file in `shaders/upscale/` declares them, in binding order —
        // the order a push's writes are appended in. The sampler last, at `FSR_BIND_SAMPLER`.

        constexpr std::array sInputs{
            Bind{ F::FSR_INPUTS_BIND_MOTION, S::Sampled, R::Motion },
            Bind{ F::FSR_INPUTS_BIND_SURFACE, S::Sampled, R::Surface },
            Bind{ F::FSR_INPUTS_BIND_COLOUR, S::Sampled, R::Colour },
            Bind{ F::FSR_INPUTS_BIND_DILATED_MOTION, S::Storage, R::DilatedMotion },
            Bind{ F::FSR_INPUTS_BIND_DILATED_DEPTH, S::Storage, R::DilatedDepth },
            Bind{ F::FSR_INPUTS_BIND_PREVIOUS_DEPTH, S::Storage, R::PreviousDepth },
            Bind{ F::FSR_INPUTS_BIND_FARTHEST_DEPTH, S::Storage, R::Intermediate },
            Bind{ F::FSR_INPUTS_BIND_CURRENT_LUMA, S::Storage, R::LumaNow },
            Bind{ F::FSR_INPUTS_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_INPUTS_BIND_PUFFS, S::Sampled, R::Puffs },
            Bind{ F::FSR_INPUTS_BIND_INPUTS, S::Block, R::Inputs },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sLumaPyramid{
            Bind{ F::FSR_PYRAMID_BIND_CURRENT_LUMA, S::Sampled, R::LumaNow },
            Bind{ F::FSR_PYRAMID_BIND_FARTHEST_DEPTH, S::Sampled, R::Intermediate },
            Bind{ F::FSR_PYRAMID_BIND_ATOMIC, S::Storage, R::SpdAtomic },
            Bind{ F::FSR_PYRAMID_BIND_FRAME_INFO, S::Storage, R::FrameInfo },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 0, S::Storage, R::SpdMips, 0 },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 1, S::Storage, R::SpdMips, 1 },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 2, S::Storage, R::SpdMips, 2 },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 3, S::Storage, R::SpdMips, 3 },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 4, S::Storage, R::SpdMips, 4 },
            Bind{ F::FSR_PYRAMID_BIND_MIP_0 + 5, S::Storage, R::SpdMips, 5 },
            Bind{ F::FSR_PYRAMID_BIND_FARTHEST_DEPTH_MIP1, S::Storage, R::FarthestDepthMip1 },
            Bind{ F::FSR_PYRAMID_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_PYRAMID_BIND_SPD, S::Block, R::Pyramid },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sChangePyramid{
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_CURRENT_LUMA, S::Sampled, R::LumaNow },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_PREVIOUS_LUMA, S::Sampled, R::LumaBefore },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_DILATED_MOTION, S::Sampled, R::DilatedMotion },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_EXPOSURE, S::Sampled, R::FrameInfo },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_ATOMIC, S::Storage, R::SpdAtomic },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 0, S::Storage, R::SpdMips, 0 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 1, S::Storage, R::SpdMips, 1 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 2, S::Storage, R::SpdMips, 2 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 3, S::Storage, R::SpdMips, 3 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 4, S::Storage, R::SpdMips, 4 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_MIP_0 + 5, S::Storage, R::SpdMips, 5 },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_CHANGE_PYRAMID_BIND_SPD, S::Block, R::Pyramid },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sChange{
            Bind{ F::FSR_CHANGE_BIND_MIPS, S::Sampled, R::SpdMips },
            Bind{ F::FSR_CHANGE_BIND_SHADING_CHANGE, S::Storage, R::ShadingChange },
            Bind{ F::FSR_CHANGE_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sReactivity{
            Bind{ F::FSR_REACTIVITY_BIND_PREVIOUS_DEPTH, S::Sampled, R::PreviousDepth },
            Bind{ F::FSR_REACTIVITY_BIND_DILATED_MOTION, S::Sampled, R::DilatedMotion },
            Bind{ F::FSR_REACTIVITY_BIND_DILATED_DEPTH, S::Sampled, R::DilatedDepth },
            Bind{ F::FSR_REACTIVITY_BIND_ACCUMULATION, S::Sampled, R::AccumulationBefore },
            Bind{ F::FSR_REACTIVITY_BIND_SHADING_CHANGE, S::Sampled, R::ShadingChange },
            Bind{ F::FSR_REACTIVITY_BIND_CURRENT_LUMA, S::Sampled, R::LumaNow },
            Bind{ F::FSR_REACTIVITY_BIND_EXPOSURE, S::Sampled, R::FrameInfo },
            Bind{ F::FSR_REACTIVITY_BIND_DILATED_MASKS, S::Storage, R::DilatedMasks },
            Bind{ F::FSR_REACTIVITY_BIND_NEW_LOCKS, S::Storage, R::NewLocks },
            Bind{ F::FSR_REACTIVITY_BIND_ACCUMULATION_OUT, S::Storage, R::AccumulationNow },
            Bind{ F::FSR_REACTIVITY_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sInstability{
            Bind{ F::FSR_INSTABILITY_BIND_EXPOSURE, S::Sampled, R::FrameInfo },
            Bind{ F::FSR_INSTABILITY_BIND_DILATED_MASKS, S::Sampled, R::DilatedMasks },
            Bind{ F::FSR_INSTABILITY_BIND_DILATED_MOTION, S::Sampled, R::DilatedMotion },
            Bind{ F::FSR_INSTABILITY_BIND_FRAME_INFO, S::Sampled, R::FrameInfo },
            Bind{ F::FSR_INSTABILITY_BIND_LUMA_HISTORY, S::Sampled, R::LumaHistoryBefore },
            Bind{ F::FSR_INSTABILITY_BIND_FARTHEST_DEPTH_MIP1, S::Sampled, R::FarthestDepthMip1 },
            Bind{ F::FSR_INSTABILITY_BIND_CURRENT_LUMA, S::Sampled, R::LumaNow },
            Bind{ F::FSR_INSTABILITY_BIND_LUMA_HISTORY_OUT, S::Storage, R::LumaHistoryNow },
            Bind{ F::FSR_INSTABILITY_BIND_INSTABILITY, S::Storage, R::Intermediate },
            Bind{ F::FSR_INSTABILITY_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        constexpr std::array sAccumulate{
            Bind{ F::FSR_ACCUMULATE_BIND_EXPOSURE, S::Sampled, R::FrameInfo },
            Bind{ F::FSR_ACCUMULATE_BIND_DILATED_MASKS, S::Sampled, R::DilatedMasks },
            Bind{ F::FSR_ACCUMULATE_BIND_DILATED_MOTION, S::Sampled, R::DilatedMotion },
            Bind{ F::FSR_ACCUMULATE_BIND_HISTORY, S::Sampled, R::HistoryBefore },
            Bind{ F::FSR_ACCUMULATE_BIND_FARTHEST_DEPTH_MIP1, S::Sampled, R::FarthestDepthMip1 },
            Bind{ F::FSR_ACCUMULATE_BIND_CURRENT_LUMA, S::Sampled, R::LumaNow },
            Bind{ F::FSR_ACCUMULATE_BIND_INSTABILITY, S::Sampled, R::Intermediate },
            Bind{ F::FSR_ACCUMULATE_BIND_COLOUR, S::Sampled, R::Colour },
            Bind{ F::FSR_ACCUMULATE_BIND_HISTORY_OUT, S::Storage, R::HistoryNow },
            Bind{ F::FSR_ACCUMULATE_BIND_OUTPUT, S::Storage, R::Output },
            Bind{ F::FSR_ACCUMULATE_BIND_NEW_LOCKS, S::Storage, R::NewLocks },
            Bind{ F::FSR_ACCUMULATE_BIND_CONSTANTS, S::Block, R::Constants },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        /// The most bindings a pass has, which is how much room one push's writes take.
        constexpr std::size_t sMostBindings = std::max({ sInputs.size(), sLumaPyramid.size(), sChangePyramid.size(),
            sChange.size(), sReactivity.size(), sInstability.size(), sAccumulate.size() });

        struct PassSpec
        {
            std::span<const Bind> mBinds;
            std::string_view mModule;
            std::string_view mName;
        };

        constexpr std::array<PassSpec, Upscaler::sPasses> sPassSpecs{ {
            { sInputs, "fsrinputs.comp.spv", "fsr inputs" },
            { sLumaPyramid, "fsrlumapyramid.comp.spv", "fsr luma pyramid" },
            { sChangePyramid, "fsrchangepyramid.comp.spv", "fsr change pyramid" },
            { sChange, "fsrchange.comp.spv", "fsr change" },
            { sReactivity, "fsrreactivity.comp.spv", "fsr reactivity" },
            { sInstability, "fsrinstability.comp.spv", "fsr instability" },
            { sAccumulate, "fsraccumulate.comp.spv", "fsr accumulate" },
        } };

        constexpr bool everyPassInBindingOrder()
        {
            for (const PassSpec& pass : sPassSpecs)
                for (std::size_t at = 1; at < pass.mBinds.size(); ++at)
                    if (pass.mBinds[at - 1].mBinding >= pass.mBinds[at].mBinding)
                        return false;
            return true;
        }
        static_assert(everyPassInBindingOrder(), "a pass's bindings out of binding order");

        ComputePipeline makePass(const Device& device, const std::filesystem::path& shaders, const PassSpec& pass)
        {
            std::array<VkDescriptorSetLayoutBinding, sMostBindings> bindings{};
            for (std::size_t at = 0; at < pass.mBinds.size(); ++at)
                bindings[at] = computeBinding(pass.mBinds[at].mBinding, typeOf(pass.mBinds[at].mAccess));

            return ComputePipeline(
                device, std::span(bindings.data(), pass.mBinds.size()), 0, {}, shaders / pass.mModule, pass.mName);
        }

        template <std::size_t... At>
        std::array<ComputePipeline, Upscaler::sPasses> makePasses(
            const Device& device, const std::filesystem::path& shaders, std::index_sequence<At...>)
        {
            return { makePass(device, shaders, sPassSpecs[At])... };
        }

        /// What every image the upscaler keeps is made for: written and read by its passes, sampled
        /// by them, and cleared at a resize and a reset.
        constexpr VkImageUsageFlags sUsage
            = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        constexpr VkClearColorValue sNought{ .float32 = { 0.0f, 0.0f, 0.0f, 0.0f } };

        /// What one dispatch does and the next may do to whatever the last one touched: every pass
        /// reads what one before it wrote, and several write what one before them read — the SDK's
        /// backend puts a barrier on every resource that changes state, which is every one. The
        /// clears stand ahead of the first pass.
        constexpr BufferUse sPassWork{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_CLEAR_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
                | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT };

        /// FSR's frame info at a reset: an exposure it has not measured, `-1`, and a luma of one.
        constexpr VkClearColorValue sFreshFrameInfo{ .float32 = { -1.0f, 1.0f, 0.0f, 0.0f } };
    }

    /// Every image the SDK's context creates, at the extents one resize names, in the SDK's formats,
    /// and the three `ffxFsr3UpscalerGetSharedResourceDescriptions` has an application make.
    struct Upscaler::Targets
    {
        Targets(const Device& device, const VkExtent2D render, const VkExtent2D output)
            : mRender(render)
            , mOutput(output)
        {
            const VkExtent2D half{ std::max(render.width / 2, 1u), std::max(render.height / 2, 1u) };
            const auto make = [&](VkExtent2D extent, VkFormat format, std::string_view name, std::uint32_t levels = 1) {
                return Image(device, extent.width, extent.height, format, sUsage, name, levels);
            };

            mAccumulation = { make(render, VK_FORMAT_R8_UNORM, "fsr-accumulation-0"),
                make(render, VK_FORMAT_R8_UNORM, "fsr-accumulation-1") };
            mLuma = { make(render, VK_FORMAT_R16_SFLOAT, "fsr-luma-0"),
                make(render, VK_FORMAT_R16_SFLOAT, "fsr-luma-1") };
            mIntermediate = make(render, VK_FORMAT_R16_SFLOAT, "fsr-intermediate");
            mShadingChange = make(half, VK_FORMAT_R8_UNORM, "fsr-shading-change");
            mNewLocks = make(output, VK_FORMAT_R8_UNORM, "fsr-new-locks");
            mHistory = { make(output, VK_FORMAT_R16G16B16A16_SFLOAT, "fsr-history-0"),
                make(output, VK_FORMAT_R16G16B16A16_SFLOAT, "fsr-history-1") };
            mSpdMips = make(half, VK_FORMAT_R16G16_SFLOAT, "fsr-spd-mips", levelsTo1x1(half.width, half.height));
            mFarthestDepthMip1 = make(half, VK_FORMAT_R16_SFLOAT, "fsr-farthest-depth-mip1");
            mLumaHistory = { make(render, VK_FORMAT_R16G16B16A16_SFLOAT, "fsr-luma-history-0"),
                make(render, VK_FORMAT_R16G16B16A16_SFLOAT, "fsr-luma-history-1") };
            mSpdAtomic = make(VkExtent2D{ 1, 1 }, VK_FORMAT_R32_UINT, "fsr-spd-atomic");
            mDilatedMasks = make(render, VK_FORMAT_R8G8B8A8_UNORM, "fsr-dilated-masks");
            mFrameInfo = make(VkExtent2D{ 1, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT, "fsr-frame-info");
            mDilatedDepth = make(render, VK_FORMAT_R32_SFLOAT, "fsr-dilated-depth");
            mDilatedMotion = make(render, VK_FORMAT_R16G16_SFLOAT, "fsr-dilated-motion");
            mPreviousDepth = make(render, VK_FORMAT_R32_UINT, "fsr-previous-depth");
            mOutputImage = make(output, VK_FORMAT_R16G16B16A16_SFLOAT, "fsr-output");
        }

        std::array<const Image*, 20> every() const
        {
            return { &mAccumulation[0], &mAccumulation[1], &mLuma[0], &mLuma[1], &mIntermediate, &mShadingChange,
                &mNewLocks, &mHistory[0], &mHistory[1], &mSpdMips, &mFarthestDepthMip1, &mLumaHistory[0],
                &mLumaHistory[1], &mSpdAtomic, &mDilatedMasks, &mFrameInfo, &mDilatedDepth, &mDilatedMotion,
                &mPreviousDepth, &mOutputImage };
        }

        VkExtent2D mRender;
        VkExtent2D mOutput;

        std::array<Image, 2> mAccumulation;
        std::array<Image, 2> mLuma;
        Image mIntermediate;
        Image mShadingChange;
        Image mNewLocks;
        std::array<Image, 2> mHistory;
        Image mSpdMips;
        Image mFarthestDepthMip1;
        std::array<Image, 2> mLumaHistory;
        Image mSpdAtomic;
        Image mDilatedMasks;
        Image mFrameInfo;
        Image mDilatedDepth;
        Image mDilatedMotion;
        Image mPreviousDepth;
        Image mOutputImage;
    };

    Upscaler::Upscaler(const Device& device, const std::filesystem::path& shaderDirectory)
        : mDevice(device)
        , mPipelines(makePasses(device, shaderDirectory, std::make_index_sequence<sPasses>{}))
        , mSampler(makeTargetSampler(device, "fsr linear clamp"))
        , mBlocks([&](const FrameSlot) {
            return Buffer::hostWritten(
                device, Shaders::FSR_BLOCKS_BYTES, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, "fsr constants");
        })
    {
    }

    Upscaler::~Upscaler() = default;

    void Upscaler::resize(const VkExtent2D render, const VkExtent2D output)
    {
        assert(render.width > 0 && render.height > 0 && output.width >= render.width && output.height >= render.height);

        mTargets = std::make_unique<Targets>(mDevice, render, output);
        mFrame.restart();

        // Every image defined from the first frame, where the SDK clears the four it reads before
        // writing and leaves the rest to its passes: a frame that read one uncleared texel differs
        // from the next run of itself, and `repeat` reads that as a hazard.
        mDevice.getPool().submitAndWait([&](VkCommandBuffer commands) {
            for (const Image* image : mTargets->every())
                image->clear(commands, Use::sUndefined, sNought, Use::sComputeReadWrite);
            mTargets->mFrameInfo.clear(commands, Use::sComputeReadWrite, sFreshFrameInfo, Use::sComputeReadWrite);
        });
    }

    void Upscaler::release()
    {
        mTargets.reset();
    }

    const Image& Upscaler::getOutput() const
    {
        assert(mTargets != nullptr && "the upscaler's output asked for before a resize made one");
        return mTargets->mOutputImage;
    }

    void Upscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs)
    {
        assert(mTargets != nullptr && "an upscale before a resize");
        Targets& targets = *mTargets;

        const Shaders::FsrConstants& constants = mFrame.advance(FsrFrame::Frame{
            .mRender = targets.mRender,
            .mOutput = targets.mOutput,
            .mCamera = inputs.mCamera,
            .mDeltaMs = inputs.mFrameDeltaMs,
            .mReset = inputs.mReset,
        });
        const bool reset = constants.mFrameIndex == 0.0f;
        const FsrFrame::Pyramid pyramid = FsrFrame::pyramidFor(targets.mRender);

        const Buffer& blocks = mBlocks.at(inputs.mSlot);
        const Shaders::FsrInputConstants inputConstants{
            .mCamera = inputs.mCamera, .mArms = inputs.mArms, .mNear = FsrFrame::sNear
        };
        blocks.writeAt(Shaders::FSR_BLOCK_CONSTANTS, std::span(&constants, 1));
        blocks.writeAt(Shaders::FSR_BLOCK_PYRAMID, std::span(&pyramid.mConstants, 1));
        blocks.writeAt(Shaders::FSR_BLOCK_INPUTS, std::span(&inputConstants, 1));
        blocks.nameForNext();

        // The SDK's pairs by the frame's parity: an odd frame reads the second of each history and
        // writes the first, and writes its luma into the second, which it reads as current and the
        // next frame reads as previous.
        const std::size_t read = mFrame.readsSecond() ? 1 : 0;
        const std::size_t written = 1 - read;

        const auto pick = [&](const Bound resource) -> const Image& {
            switch (resource)
            {
                case R::Motion:
                    return inputs.mMotion;
                case R::Surface:
                    return inputs.mSurface;
                case R::Colour:
                    return inputs.mColour;
                case R::Puffs:
                    return inputs.mPuffs;
                case R::DilatedMotion:
                    return targets.mDilatedMotion;
                case R::DilatedDepth:
                    return targets.mDilatedDepth;
                case R::PreviousDepth:
                    return targets.mPreviousDepth;
                case R::Intermediate:
                    return targets.mIntermediate;
                case R::LumaNow:
                    return targets.mLuma[read];
                case R::LumaBefore:
                    return targets.mLuma[written];
                case R::SpdAtomic:
                    return targets.mSpdAtomic;
                case R::FrameInfo:
                    return targets.mFrameInfo;
                case R::SpdMips:
                    return targets.mSpdMips;
                case R::FarthestDepthMip1:
                    return targets.mFarthestDepthMip1;
                case R::ShadingChange:
                    return targets.mShadingChange;
                case R::AccumulationBefore:
                    return targets.mAccumulation[read];
                case R::AccumulationNow:
                    return targets.mAccumulation[written];
                case R::DilatedMasks:
                    return targets.mDilatedMasks;
                case R::NewLocks:
                    return targets.mNewLocks;
                case R::LumaHistoryBefore:
                    return targets.mLumaHistory[read];
                case R::LumaHistoryNow:
                    return targets.mLumaHistory[written];
                case R::HistoryBefore:
                    return targets.mHistory[read];
                case R::HistoryNow:
                    return targets.mHistory[written];
                case R::Output:
                    return targets.mOutputImage;
                case R::Constants:
                case R::Pyramid:
                case R::Inputs:
                case R::Sampler:
                    break;
            }

            Crash::fatal("an upscaler resource that is not an image");
        };

        const auto blockOf = [&](const Bound resource) -> VkDescriptorBufferInfo {
            switch (resource)
            {
                case R::Constants:
                    return { blocks.getHandle(), Shaders::FSR_BLOCK_CONSTANTS, sizeof(Shaders::FsrConstants) };
                case R::Pyramid:
                    return { blocks.getHandle(), Shaders::FSR_BLOCK_PYRAMID, sizeof(Shaders::FsrPyramidConstants) };
                case R::Inputs:
                    return { blocks.getHandle(), Shaders::FSR_BLOCK_INPUTS, sizeof(Shaders::FsrInputConstants) };
                default:
                    break;
            }

            Crash::fatal("an upscaler resource that is not a constant block");
        };

        const auto between = [&] { handOver(commands, sPassWork, sPassWork); };

        const auto run = [&](const Pass pass, const std::uint32_t groupsX, const std::uint32_t groupsY) {
            const PassSpec& spec = sPassSpecs[static_cast<std::size_t>(pass)];
            DescriptorWrites<sMostBindings> writes;
            for (const Bind& bind : spec.mBinds)
            {
                switch (bind.mAccess)
                {
                    case Access::Sampled:
                        writes.image(bind.mBinding, pick(bind.mBound).describeSampled(VK_NULL_HANDLE),
                            VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
                        break;
                    case Access::Storage:
                        writes.image(bind.mBinding, pick(bind.mBound).describeStorage(bind.mLevel));
                        break;
                    case Access::Block:
                        writes.buffer(bind.mBinding, blockOf(bind.mBound), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
                        break;
                    case Access::Sampler:
                        writes.image(bind.mBinding,
                            VkDescriptorImageInfo{ mSampler.get(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED },
                            VK_DESCRIPTOR_TYPE_SAMPLER);
                        break;
                }
            }

            const ComputePipeline& pipeline = mPipelines[static_cast<std::size_t>(pass)];
            bind(commands, pipeline);
            pushDescriptors(commands, pipeline, writes.get());
            vkCmdDispatch(commands, groupsX, groupsY, 1);
        };

        // The SDK's clears, ahead of the passes: at a reset, the accumulation read this frame, the
        // SPD mips and an unmeasured exposure; every frame, the reconstructed depth its atomics take
        // the largest into, the SPD counter, and the SPD mips again, which the SDK clears so a level
        // is never read where nothing wrote it.
        if (reset)
        {
            targets.mAccumulation[read].clear(commands, Use::sComputeReadWrite, sNought, Use::sComputeReadWrite);
            targets.mFrameInfo.clear(commands, Use::sComputeReadWrite, sFreshFrameInfo, Use::sComputeReadWrite);
        }
        targets.mPreviousDepth.clear(commands, Use::sComputeReadWrite, sNought, Use::sComputeReadWrite);
        targets.mSpdAtomic.clear(commands, Use::sComputeReadWrite, sNought, Use::sComputeReadWrite);
        targets.mSpdMips.clear(commands, Use::sComputeReadWrite, sNought, Use::sComputeReadWrite);
        between();

        const VkExtent2D render = targets.mRender;
        const VkExtent2D output = targets.mOutput;
        const std::uint32_t renderX = groupsFor(render.width, Shaders::FSR_WORKGROUP);
        const std::uint32_t renderY = groupsFor(render.height, Shaders::FSR_WORKGROUP);

        run(Pass::Inputs, renderX, renderY);
        between();
        run(Pass::LumaPyramid, pyramid.mGroupsX, pyramid.mGroupsY);
        between();
        run(Pass::ChangePyramid, pyramid.mGroupsX, pyramid.mGroupsY);
        between();
        run(Pass::Change, groupsFor(render.width / 2, Shaders::FSR_WORKGROUP),
            groupsFor(render.height / 2, Shaders::FSR_WORKGROUP));
        between();
        run(Pass::Reactivity, renderX, renderY);
        between();
        run(Pass::Instability, renderX, renderY);
        between();
        run(Pass::Accumulate, groupsFor(output.width, Shaders::FSR_WORKGROUP),
            groupsFor(output.height, Shaders::FSR_WORKGROUP));
    }
}
