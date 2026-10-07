#include "accumulatepass.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <string_view>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>
#include <components/rtxvulkan/trace/gbuffer.hpp>

namespace Rtx
{
    namespace
    {
        /// The channel being blended, the two the frame describes it with, the three a history
        /// arrives in, the two of those this pass writes back, and the blend the cascade reads; the
        /// fill's channel, history and blend; and the fast means, read and written. The slow colour
        /// histories are read and not written, because the first wavelet level writes the history
        /// this reads next frame — SVGF's feedback.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_BINDINGS> sBindings
            = computeBindings<Shaders::ACCUMULATE_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        /// The surface, the fast blends and the frame's samples read, the slow means rewritten in
        /// place, and the fast means written.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ACCUMULATE_CLAMP_BINDINGS> sClampBindings
            = computeBindings<Shaders::ACCUMULATE_CLAMP_BINDINGS>(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

        /// The clamp with the ring's ceiling or without it (`ACCUMULATE_CLAMP_SPEC_RING`).
        ComputePipeline<Shaders::AccumulateClampConstants> makeClamp(
            const Device& device, const bool ring, const std::string_view name)
        {
            std::array<std::uint32_t, Shaders::ACCUMULATE_CLAMP_SPEC_COUNT> specialization{};
            specialization[Shaders::ACCUMULATE_CLAMP_SPEC_RING] = ring ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::AccumulateClampConstants>(
                device, sClampBindings, {}, "accumulateclamp.comp.spv", name, specialization);
        }
    }

    AccumulatePass::AccumulatePass(const Device& device)
        : mPipeline(device, sBindings, {}, "accumulate.comp.spv", "accumulate")
        , mClamp(makeClamp(device, false, "accumulate-clamp"))
        , mClampRing(makeClamp(device, true, "accumulate-clamp-ring"))
    {
    }

    void AccumulatePass::record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        assert(images.mBlended.getWidth() >= camera.mWidth && images.mBlended.getHeight() >= camera.mHeight);

        DescriptorWrites writes(mPipeline);
        writes.image(Shaders::ACCUMULATE_BIND_INDIRECT, buffer.get(Channel::Indirect).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOTION, buffer.get(Channel::Motion).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_COLOUR, images.mColourBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_SURFACE, images.mSurfaceBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_MOMENTS, images.mMomentsBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_SURFACE_OUT, images.mSurface.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_MOMENTS_OUT, images.mMoments.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_BLENDED_OUT, images.mBlended.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FILL, buffer.get(Channel::Fill).describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_FILL, images.mFillBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FILL_BLENDED_OUT, images.mFillBlended.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_HISTORY_FAST, images.mFastBefore.describeStorage());
        writes.image(Shaders::ACCUMULATE_BIND_FAST_OUT, images.mFastBlended.describeStorage());

        const Shaders::AccumulateConstants constants{
            .mHistory = frame.history(images.mFresh),
            .mDualMotion = frame.mDualMotion ? 1u : 0u,
        };

        dispatch(commands, mPipeline, writes, constants,
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::ACCUMULATE_WORKGROUP));
    }

    void AccumulatePass::recordClamp(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;

        // The clamp reads a neighbour's fast blend and samples, so every pixel's blend is behind it,
        // and the count the accumulator wrote beside it.
        Barriers blended(commands);
        for (const Image* image : { &images.mBlended, &images.mFillBlended, &images.mFastBlended })
            image->addTransition(blended, Use::sComputeWrite, Use::sComputeReadWrite);
        images.mMoments.addTransition(blended, Use::sComputeWrite, Use::sComputeRead);
        blended.flush();

        const ComputePipeline<Shaders::AccumulateClampConstants>& clamp = frame.mAntiFirefly ? mClampRing : mClamp;
        DescriptorWrites clampWrites(clamp);
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SURFACE, buffer.get(Channel::Surface).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FAST, images.mFastBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_BLENDED, images.mBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FILL_BLENDED, images.mFillBlended.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SAMPLED, buffer.get(Channel::Indirect).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_SAMPLED_FILL, buffer.get(Channel::Fill).describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_FAST_OUT, images.mFast.describeStorage());
        clampWrites.image(Shaders::ACCUMULATE_CLAMP_BIND_MOMENTS, images.mMoments.describeStorage());

        dispatch(commands, clamp, clampWrites,
            Shaders::AccumulateClampConstants{ .mEyes = frame.mSampled.mEyes, .mAntilag = frame.mAntilag ? 1u : 0u },
            Groups::covering(camera.mWidth, camera.mHeight, Shaders::ACCUMULATE_WORKGROUP));
    }
}
