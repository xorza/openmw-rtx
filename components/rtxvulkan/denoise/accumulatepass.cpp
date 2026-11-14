#include "accumulatepass.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>

namespace Rtx
{
    namespace
    {
        /// Every binding is a storage image. The slow colour histories are read and not written, because
        /// the first wavelet level writes the history this reads next frame — SVGF's feedback.
        ///
        /// And the exposure the frame before was shown at, a buffer, last.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_BINDINGS> sBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_BINDINGS> bindings{};
            const auto images = computeBindings<Shaders::ACCUMULATE_BIND_EXPOSURE>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            std::copy(images.begin(), images.end(), bindings.begin());
            bindings[Shaders::ACCUMULATE_BIND_EXPOSURE]
                = computeBinding(Shaders::ACCUMULATE_BIND_EXPOSURE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            return bindings;
        }();

        /// The surface, the fast blends and the frame's samples read, the slow means rewritten in
        /// place, and the fast means written; and the exposure the accumulator held the samples under, a
        /// buffer, last.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_CLAMP_BINDINGS> sClampBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_CLAMP_BINDINGS> bindings{};
            const auto images
                = computeBindings<Shaders::ACCUMULATE_CLAMP_BIND_EXPOSURE>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            std::copy(images.begin(), images.end(), bindings.begin());
            bindings[Shaders::ACCUMULATE_CLAMP_BIND_EXPOSURE]
                = computeBinding(Shaders::ACCUMULATE_CLAMP_BIND_EXPOSURE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            return bindings;
        }();

        ComputePipeline<Shaders::AccumulateConstants> makeAccumulate(
            const Device& device, const bool fill, const std::string_view name)
        {
            std::array<std::uint32_t, Shaders::ACCUMULATE_SPEC_COUNT> specialization{};
            specialization[Shaders::ACCUMULATE_SPEC_FILL] = fill ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::AccumulateConstants>(
                device, sBindings, {}, "accumulate.comp.spv", name, specialization);
        }

        /// The clamp with the ring's ceiling or without it (`ACCUMULATE_CLAMP_SPEC_RING`).
        ComputePipeline<Shaders::AccumulateClampConstants> makeClamp(
            const Device& device, const bool ring, const bool fill, const std::string_view name)
        {
            std::array<std::uint32_t, Shaders::ACCUMULATE_CLAMP_SPEC_COUNT> specialization{};
            specialization[Shaders::ACCUMULATE_CLAMP_SPEC_RING] = ring ? VK_TRUE : VK_FALSE;
            specialization[Shaders::ACCUMULATE_CLAMP_SPEC_FILL] = fill ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::AccumulateClampConstants>(
                device, sClampBindings, {}, "accumulateclamp.comp.spv", name, specialization);
        }
    }

    namespace
    {
        /// Whether the frame traces a fill, the low bit of a pipeline's place.
        std::size_t fillOf(const DenoiseFrame& frame)
        {
            return frame.mSampled.mBounceRays != 0 ? 1u : 0u;
        }
    }

    AccumulatePass::AccumulatePass(const Device& device)
        : mPipelines{ makeAccumulate(device, false, "accumulate-no-fill"), makeAccumulate(device, true, "accumulate") }
        , mClamps{ makeClamp(device, false, false, "accumulate-clamp-no-fill"),
            makeClamp(device, false, true, "accumulate-clamp"),
            makeClamp(device, true, false, "accumulate-clamp-ring-no-fill"),
            makeClamp(device, true, true, "accumulate-clamp-ring") }
    {
    }

    void AccumulatePass::record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        assert(images.mBlended.getWidth() >= camera.mWidth);
        assert(images.mBlended.getHeight() >= camera.mHeight);

        const ComputePipeline<Shaders::AccumulateConstants>& pipeline = mPipelines[fillOf(frame)];
        DescriptorWrites writes(pipeline);
        writes.image(Shaders::ACCUMULATE_BIND_INDIRECT, buffer.get(Channel::Indirect).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_COLOUR, images.mColour.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_SURFACE, buffer.getHeld(Channel::Surface).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_MOMENTS, images.mMomentsBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOMENTS_OUT, images.mMoments.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_BLENDED_OUT, images.mBlended.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FILL, buffer.get(Channel::Fill).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_FILL, images.mFill.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FILL_BLENDED_OUT, images.mFillBlended.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_FAST, images.mFast.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FAST_OUT, images.mFastBlended.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MATCH_OUT, images.mMatch.describeStorage());
        writes.buffer(Shaders::ACCUMULATE_BIND_EXPOSURE, frame.mExposure.describe());

        const Shaders::AccumulateConstants constants{
            .mHistory = frame.history(images.mFresh),
            .mDualMotion = frame.mFilters.mDualMotion ? 1u : 0u,
            .mFireflyHold = frame.mFilters.mAntiFirefly ? 1u : 0u,
        };

        dispatch(commands, pipeline, writes, constants,
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::ACCUMULATE_WORKGROUP));
    }

    void AccumulatePass::recordClamp(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;

        const ComputePipeline<Shaders::AccumulateClampConstants>& clamp
            = mClamps[(frame.mFilters.mAntiFirefly ? 2u : 0u) | fillOf(frame)];
        DescriptorWrites clampWrites(clamp);
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FAST, images.mFastBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_BLENDED, images.mBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FILL_BLENDED, images.mFillBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SAMPLED, buffer.get(Channel::Indirect).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SAMPLED_FILL, buffer.get(Channel::Fill).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FAST_OUT, images.mFast.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_MOMENTS, images.mMoments.describeStorage());
        clampWrites.buffer(Shaders::ACCUMULATE_CLAMP_BIND_EXPOSURE, frame.mExposure.describe());

        dispatch(commands, clamp, clampWrites,
            Shaders::AccumulateClampConstants{ .mEyes = frame.mSampled.mEyes,
                .mAntilag = frame.mFilters.mAntilag ? 1u : 0u,
                .mFrame = frame.mSampled.mFrame },
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::ACCUMULATE_WORKGROUP));
    }
}
