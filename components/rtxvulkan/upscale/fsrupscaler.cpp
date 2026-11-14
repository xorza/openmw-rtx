// Derived from the FidelityFX SDK (v1.1.4), `ffx_fsr3upscaler.cpp`, whose notice follows.
//
// Copyright (C) 2024 Advanced Micro Devices, Inc.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "fsrupscaler.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include <components/crashcatcher/crash.hpp>
#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/image/texturedata.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/device.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/frame/framestep.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>
#include <components/rtxvulkan/shaders/shared/fsr.h>
#include <components/rtxvulkan/shaders/shared/upscaleinputs.h>

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
            Masks,
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
            Bind{ F::FSR_INPUTS_BIND_INPUTS, S::Block, R::Inputs },
            Bind{ F::FSR_BIND_SAMPLER, S::Sampler, R::Sampler },
        };

        // One binding a level of the SPD image, listed, and as many as the shader declares.
        static_assert(Shaders::FSR_PYRAMID_MIPS == 6, "the pyramid's mip bindings below are one a level");
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
            Bind{ F::FSR_REACTIVITY_BIND_MASKS, S::Sampled, R::Masks },
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

        constexpr std::array<PassSpec, FsrUpscaler::sPasses> sPassSpecs{ {
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

        constexpr bool everyPassHasAModule()
        {
            for (const PassSpec& pass : sPassSpecs)
                if (pass.mModule.empty())
                    return false;
            return true;
        }
        static_assert(everyPassHasAModule(), "a pass `Pass` names and `sPassSpecs` leaves out");

        ComputePipeline<NoConstants> makePass(const Device& device, const PassSpec& pass)
        {
            std::array<VkDescriptorSetLayoutBinding, sMostBindings> bindings{};
            for (std::size_t at = 0; at < pass.mBinds.size(); ++at)
                bindings[at] = computeBinding(pass.mBinds[at].mBinding, typeOf(pass.mBinds[at].mAccess));

            return ComputePipeline<NoConstants>(
                device, std::span(bindings.data(), pass.mBinds.size()), {}, pass.mModule, pass.mName);
        }

        template <std::size_t... At>
        std::array<ComputePipeline<NoConstants>, FsrUpscaler::sPasses> makePasses(
            const Device& device, std::index_sequence<At...>)
        {
            return { makePass(device, sPassSpecs[At])... };
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

        /// FSR's frame info at a reset: an exposure it has not measured, `-1`, as the SDK clears it, and a log luma of
        /// `1e8`, over `resetAutoExposureAverageSmoothing`, which tells its luma pyramid there is no average to smooth
        /// from. **Not the SDK's one**: the 3.1.4 host clears the log luma to `1`
        /// (`ffx_fsr3upscaler.cpp`, "Clear Frame Info"), which is under that bound, so the pyramid smooths the reset
        /// frame's luma from one as though it had been measured, where the bound's name promises no smoothing. FSR
        /// meters its own exposure and not the display's: handed the display's, last frame's, the still frames stood
        /// further from the converged reference (the guild's bias 1.20 -> 1.58, the pond 1.51 -> 1.71, the pier 1.32
        /// -> 1.39).
        constexpr VkClearColorValue sFreshFrameInfo{ .float32 = { -1.0f, 1e8f, 0.0f, 0.0f } };
    }

    namespace
    {
        /// The images a frame writes whole before it reads them and no frame after reads, which the frame's arena
        /// places (`ImageArena`): those the SDK flags aliasable (`FFX_RESOURCE_FLAGS_ALIASABLE`), and the three it has
        /// an application make, which the first pass writes whole or a frame clears. Not the new locks, which the SDK
        /// flags aliasable too: the accumulation pass clears them for the next frame's reactivity pass, which sets only
        /// the locks it finds (`ffx_fsr3upscaler_accumulate.h`). In the order `declareShared` declares them.
        enum class Shared : std::uint8_t
        {
            Intermediate,
            ShadingChange,
            SpdMips,
            FarthestDepthMip1,
            DilatedMasks,
            DilatedDepth,
            DilatedMotion,
            PreviousDepth,
            Output,
        };

        constexpr std::size_t sSharedCount = static_cast<std::size_t>(Shared::Output) + 1;

        /// Which extent an image is made at: the trace's, half of it, or the output's.
        enum class Grid : std::uint8_t
        {
            Render,
            Half,
            Output,
        };

        struct SharedDeclared
        {
            Shared mImage;
            Grid mGrid;
            VkFormat mFormat;
            std::string_view mName;

            /// Whether it holds a chain down to one texel, which the luma pyramid writes.
            bool mChain = false;
        };

        constexpr std::array<SharedDeclared, sSharedCount> sShared{ {
            { Shared::Intermediate, Grid::Render, toVulkanFormat(FSR_INTERMEDIATE_FORMAT), "fsr-intermediate" },
            { Shared::ShadingChange, Grid::Half, toVulkanFormat(FSR_SHADING_CHANGE_FORMAT), "fsr-shading-change" },
            { Shared::SpdMips, Grid::Half, toVulkanFormat(FSR_SPD_MIPS_FORMAT), "fsr-spd-mips", true },
            { Shared::FarthestDepthMip1, Grid::Half, toVulkanFormat(FSR_FARTHEST_DEPTH_MIP1_FORMAT),
                "fsr-farthest-depth-mip1" },
            { Shared::DilatedMasks, Grid::Render, toVulkanFormat(FSR_DILATED_MASKS_FORMAT), "fsr-dilated-masks" },
            { Shared::DilatedDepth, Grid::Render, toVulkanFormat(FSR_DILATED_DEPTH_FORMAT), "fsr-dilated-depth" },
            { Shared::DilatedMotion, Grid::Render, toVulkanFormat(FSR_DILATED_MOTION_FORMAT), "fsr-dilated-motion" },
            { Shared::PreviousDepth, Grid::Render, toVulkanFormat(FSR_PREVIOUS_DEPTH_FORMAT), "fsr-previous-depth" },
            { Shared::Output, Grid::Output, toVulkanFormat(FSR_OUTPUT_FORMAT), "fsr-output" },
        } };

        constexpr bool sharedInOrder()
        {
            for (std::size_t at = 0; at < sShared.size(); ++at)
                if (static_cast<std::size_t>(sShared[at].mImage) != at)
                    return false;
            return true;
        }

        static_assert(sharedInOrder(), "the shared table is indexed by Shared");

        ImageDescription imageOf(const VkExtent2D extent, const VkFormat format, const std::uint32_t levels = 1)
        {
            return ImageDescription{ .mWidth = extent.width,
                .mHeight = extent.height,
                .mFormat = format,
                .mUsage = sUsage,
                .mMipLevels = levels };
        }

        ImageDescription descriptionOf(const SharedDeclared& declared, const VkExtent2D render, const VkExtent2D output)
        {
            const VkExtent2D extent = declared.mGrid == Grid::Render
                ? render
                : (declared.mGrid == Grid::Half ? FsrFrame::halfOf(render) : output);
            return imageOf(extent, declared.mFormat, declared.mChain ? levelsTo1x1(extent.width, extent.height) : 1);
        }

        /// What every pass may do to an image: `sPassWork` in the general layout, which is where the
        /// shared images are laid out at the start of a frame's reconstruction.
        constexpr ImageUse sPassImage{ VK_IMAGE_LAYOUT_GENERAL, sPassWork.mStage, sPassWork.mAccess };
    }

    /// Every image the SDK's context creates, at the extents one resize names, in the SDK's formats,
    /// and the three `ffxFsr3UpscalerGetSharedResourceDescriptions` has an application make: its own
    /// for what a frame carries to the next, and the frame arena's for the rest (`Shared`).
    struct FsrUpscaler::Targets
    {
        /// Each image of its own as `image` makes it from its description and name: the frame's own
        /// memory, or nothing where `FsrUpscaler::bytesAt` only adds up what each would take, which so
        /// measures the one list this makes.
        template <class MakeImage>
        Targets(const VkExtent2D render, const VkExtent2D output, MakeImage&& image)
            : mRender(render)
            , mOutput(output)
            , mPyramid(FsrFrame::pyramidFor(render))
        {
            const auto make = [&](VkExtent2D extent, VkFormat format, std::string_view name) {
                return image(imageOf(extent, format), name);
            };

            add(mAccumulation[0], make(render, toVulkanFormat(FSR_ACCUMULATION_FORMAT), "fsr-accumulation-0"));
            add(mAccumulation[1], make(render, toVulkanFormat(FSR_ACCUMULATION_FORMAT), "fsr-accumulation-1"));
            add(mLuma[0], make(render, toVulkanFormat(FSR_LUMA_FORMAT), "fsr-luma-0"));
            add(mLuma[1], make(render, toVulkanFormat(FSR_LUMA_FORMAT), "fsr-luma-1"));
            add(mNewLocks, make(output, toVulkanFormat(FSR_NEW_LOCKS_FORMAT), "fsr-new-locks"));
            add(mHistory[0], make(output, toVulkanFormat(FSR_HISTORY_FORMAT), "fsr-history-0"));
            add(mHistory[1], make(output, toVulkanFormat(FSR_HISTORY_FORMAT), "fsr-history-1"));
            add(mLumaHistory[0], make(render, toVulkanFormat(FSR_LUMA_HISTORY_FORMAT), "fsr-luma-history-0"));
            add(mLumaHistory[1], make(render, toVulkanFormat(FSR_LUMA_HISTORY_FORMAT), "fsr-luma-history-1"));
            add(mSpdAtomic, make(VkExtent2D{ 1, 1 }, toVulkanFormat(FSR_SPD_ATOMIC_FORMAT), "fsr-spd-atomic"));
            add(mFrameInfo, make(VkExtent2D{ 1, 1 }, toVulkanFormat(FSR_FRAME_INFO_FORMAT), "fsr-frame-info"));
        }

        Targets(const Targets&) = delete;
        Targets& operator=(const Targets&) = delete;
        Targets(Targets&&) = delete;
        Targets& operator=(Targets&&) = delete;
        ~Targets() = default;

        /// Every image of its own the constructor made, which is what the first record lays out and
        /// clears: the one list `add` fills, so an image cannot be made and left out of it.
        const std::vector<const Image*>& every() const { return mEvery; }

        const Image& shared(const Shared which) const { return mShared[static_cast<std::size_t>(which)]; }

        VkExtent2D mRender;
        VkExtent2D mOutput;

        /// How the luma pyramid is dispatched over the render extent, which is all it depends on.
        FsrFrame::Pyramid mPyramid;

        std::array<Image, 2> mAccumulation;
        std::array<Image, 2> mLuma;
        Image mNewLocks;
        std::array<Image, 2> mHistory;
        std::array<Image, 2> mLumaHistory;
        Image mSpdAtomic;
        Image mFrameInfo;

        /// The images `declareShared` declared, as the frame's arena placed them.
        ArenaRun mShared;

        /// Whether a record laid out and cleared what `every` lists, which the first does.
        bool mLaidOut = false;

    private:
        std::vector<const Image*> mEvery;

        void add(Image& member, Image image)
        {
            member = std::move(image);
            mEvery.push_back(&member);
        }
    };

    FsrUpscaler::FsrUpscaler(const Device& device)
        : mDevice(device)
        , mPipelines(makePasses(device, std::make_index_sequence<sPasses>{}))
        , mSampler(makeTargetSampler(device, "fsr linear clamp"))
        , mBlocks(Buffer::deviceLocal(device, Shaders::FSR_BLOCKS_BYTES,
              VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, "fsr constants"))
    {
    }

    FsrUpscaler::~FsrUpscaler() = default;

    void FsrUpscaler::declareShared(const VkExtent2D render, const VkExtent2D output, std::vector<ArenaImage>& into)
    {
        for (const SharedDeclared& declared : sShared)
            into.push_back(ArenaImage{ .mDescription = descriptionOf(declared, render, output),
                .mSpan = declared.mImage == Shared::Output ? spanOf(FrameStep::Upscale, FrameStep::Display)
                                                           : spanOf(FrameStep::Upscale, FrameStep::Upscale),
                .mName = declared.mName });
    }

    std::size_t FsrUpscaler::getSharedCount()
    {
        return sSharedCount;
    }

    void FsrUpscaler::resize(const Upscale, const VkExtent2D render, const VkExtent2D output, const ArenaRun& shared)
    {
        assert(render.width > 0 && render.height > 0 && output.width >= render.width && output.height >= render.height);
        assert(shared.size() == sSharedCount && "the upscaler's shared images were not placed as it declared them");

        // Targets that stand are asked at their own extents: the frame's owner decides what moved,
        // and lets go of targets that did (`FrameTracer::Moves`).
        if (mTargets != nullptr)
        {
            assert(isAt(render, output) && "FSR's targets asked for other extents, which the frame lets go of first");
            mTargets->mShared = shared;
            return;
        }

        mTargets = std::make_unique<Targets>(
            render, output, [&](const ImageDescription& description, std::string_view name) {
                return Image(MemoryUse::Frame, mDevice, description, name);
            });
        mTargets->mShared = shared;
        mFrame.restart();
    }

    void FsrUpscaler::release()
    {
        mTargets.reset();
    }

    bool FsrUpscaler::isAt(const VkExtent2D render, const VkExtent2D output) const
    {
        const auto same = [](VkExtent2D a, VkExtent2D b) { return a.width == b.width && a.height == b.height; };
        return mTargets != nullptr && same(mTargets->mRender, render) && same(mTargets->mOutput, output);
    }

    VkDeviceSize FsrUpscaler::bytesAt(const VkExtent2D render, const VkExtent2D output) const
    {
        VkDeviceSize bytes = 0;
        const Targets measured(render, output, [&](const ImageDescription& description, std::string_view) {
            bytes += Image::bytesFor(mDevice, description);
            return Image();
        });
        return bytes;
    }

    HandedImage FsrUpscaler::record(const VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer)
    {
        const GpuZone timed(timer, commands, FrameZone::Upscale);

        assert(mTargets != nullptr && "an upscale before a resize");
        assert(inputs.mColour.mLeftAs.mLayout == VK_IMAGE_LAYOUT_GENERAL && "FSR samples the colour where it stands");
        Targets& targets = *mTargets;

        const bool reset = mFrame.isFresh();
        const Shaders::FsrConstants& constants = mFrame.advance(FsrFrame::Frame{
            .mRender = targets.mRender,
            .mOutput = targets.mOutput,
            .mCamera = inputs.mEyes.mWorld,
            .mPreviousJitter = inputs.mPreviousJitter,
            .mJitterPhases = inputs.mJitterPhases,
            .mSeconds = inputs.mSeconds,
        });
        const FsrFrame::Pyramid& pyramid = targets.mPyramid;

        const Buffer& blocks = mBlocks;
        const Shaders::UpscaleInputConstants inputConstants{
            .mEyes = inputs.mEyes, .mNear = sUpscaleNear, .mFrame = inputs.mFrame
        };
        std::array<std::byte, Shaders::FSR_BLOCKS_BYTES> blockBytes{};
        const auto place = [&](const std::uint32_t offset, const auto& block) {
            std::ranges::copy(std::as_bytes(std::span(&block, 1)), blockBytes.begin() + offset);
        };
        place(Shaders::FSR_BLOCK_CONSTANTS, constants);
        place(Shaders::FSR_BLOCK_PYRAMID, pyramid.mConstants);
        place(Shaders::FSR_BLOCK_INPUTS, inputConstants);
        blocks.updateInline(commands, Use::sBufferUniformRead, blockBytes);

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
                    return inputs.mColour.mImage;
                case R::Masks:
                    return inputs.mMasks;
                case R::DilatedMotion:
                    return targets.shared(Shared::DilatedMotion);
                case R::DilatedDepth:
                    return targets.shared(Shared::DilatedDepth);
                case R::PreviousDepth:
                    return targets.shared(Shared::PreviousDepth);
                case R::Intermediate:
                    return targets.shared(Shared::Intermediate);
                case R::LumaNow:
                    return targets.mLuma[read];
                case R::LumaBefore:
                    return targets.mLuma[written];
                case R::SpdAtomic:
                    return targets.mSpdAtomic;
                case R::FrameInfo:
                    return targets.mFrameInfo;
                case R::SpdMips:
                    return targets.shared(Shared::SpdMips);
                case R::FarthestDepthMip1:
                    return targets.shared(Shared::FarthestDepthMip1);
                case R::ShadingChange:
                    return targets.shared(Shared::ShadingChange);
                case R::AccumulationBefore:
                    return targets.mAccumulation[read];
                case R::AccumulationNow:
                    return targets.mAccumulation[written];
                case R::DilatedMasks:
                    return targets.shared(Shared::DilatedMasks);
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
                    return targets.shared(Shared::Output);
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
                    return { blocks.getHandle(), Shaders::FSR_BLOCK_INPUTS, sizeof(Shaders::UpscaleInputConstants) };
                default:
                    break;
            }

            Crash::fatal("an upscaler resource that is not a constant block");
        };

        const auto between = [&] { handOver(commands, sPassWork, sPassWork); };

        const auto run = [&](const Pass pass, const Groups groups) {
            const PassSpec& spec = sPassSpecs[static_cast<std::size_t>(pass)];
            const ComputePipeline<NoConstants>& pipeline = mPipelines[static_cast<std::size_t>(pass)];
            DescriptorWrites writes(pipeline);
            for (const Bind& bind : spec.mBinds)
            {
                switch (bind.mAccess)
                {
                    case Access::Sampled:
                        writes.image(bind.mBinding, pick(bind.mBound).describeSampled(VK_NULL_HANDLE));
                        break;
                    case Access::Storage:
                    {
                        // **A level past the chain's end is bound as its last**, as the SDK's Vulkan
                        // backend binds it (`ffx_vk.cpp`): the pyramid passes declare six levels and
                        // write only the `MipCount` a small render extent has, and every binding the
                        // layout declares still needs a view.
                        const Image& image = pick(bind.mBound);
                        writes.image(
                            bind.mBinding, image.describeStorage(std::min(bind.mLevel, image.getMipLevels() - 1)));
                        break;
                    }
                    case Access::Block:
                        writes.buffer(bind.mBinding, blockOf(bind.mBound));
                        break;
                    case Access::Sampler:
                        writes.image(bind.mBinding,
                            VkDescriptorImageInfo{ mSampler.get(), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED });
                        break;
                }
            }

            dispatch(commands, pipeline, writes, NoConstants{}, groups);
        };

        // The SDK's clears, ahead of the passes: at a reset, the accumulation read this frame, the SPD mips and an
        // unmeasured exposure; every frame, the reconstructed depth its atomics take the largest into, the SPD counter,
        // and the SPD mips again, which the SDK clears so a level is never read where nothing wrote it.
        //
        // In the general layout the passes use, and ordered by the one barrier after them: the last frame's passes over
        // these images are behind the head barrier the buffer opens with (`CommandPool::begin`), so a transition into a
        // clear's own layout and back was two barriers an image ordering nothing between does not need.
        //
        // The images the frame's arena holds are laid out first, from what held their memory before them in this
        // buffer: the SDK clears two of them, and the passes write the rest whole before reading them. Its own images
        // are laid out by the first frame after they were made, in its own buffer and not in a submit a resize waits
        // for, and cleared whole: a frame that read one uncleared texel differs from the next run of itself, which
        // `repeat` reads as a hazard. The first frame is a reset, whose clears follow behind one barrier more.
        const bool unlaid = !std::exchange(targets.mLaidOut, true);
        Barriers laid(commands);
        if (unlaid)
            for (const Image* image : targets.every())
                image->addTransition(laid, Use::sUndefined, sPassImage);
        for (std::size_t at = 0; at < sSharedCount; ++at)
            targets.mShared[at].addTransition(laid, targets.mShared.firstUseOf(at), sPassImage);
        laid.flush();
        if (unlaid)
        {
            assert(reset && "the first frame of new targets reads a history");
            for (const Image* image : targets.every())
                image->clearInGeneral(commands, sNought);
            between();
        }

        if (reset)
        {
            targets.mAccumulation[read].clearInGeneral(commands, sNought);
            targets.mFrameInfo.clearInGeneral(commands, sFreshFrameInfo);
        }
        targets.shared(Shared::PreviousDepth).clearInGeneral(commands, sNought);
        targets.mSpdAtomic.clearInGeneral(commands, sNought);
        targets.shared(Shared::SpdMips).clearInGeneral(commands, sNought);
        between();

        const VkExtent2D render = targets.mRender;
        const VkExtent2D output = targets.mOutput;
        const Groups overRender = Groups::covering(render.width, render.height, Shaders::FSR_WORKGROUP);
        const Groups overPyramid{ .mX = pyramid.mGroupsX, .mY = pyramid.mGroupsY };

        run(Pass::Inputs, overRender);
        between();
        run(Pass::LumaPyramid, overPyramid);
        between();
        run(Pass::ChangePyramid, overPyramid);
        between();
        const VkExtent2D half = FsrFrame::halfOf(render);
        run(Pass::Change, Groups::covering(half.width, half.height, Shaders::FSR_WORKGROUP));
        between();
        run(Pass::Reactivity, overRender);
        between();
        run(Pass::Instability, overRender);
        between();
        run(Pass::Accumulate, Groups::covering(output.width, output.height, Shaders::FSR_WORKGROUP));
        return HandedImage{ .mImage = targets.shared(Shared::Output), .mLeftAs = Use::sComputeReadWrite };
    }
}
