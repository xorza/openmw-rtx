#include "atrouspass.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtx/shaders/look.h>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/formats.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>
#include <components/rtxvulkan/pipeline/dispatch.hpp>
#include <components/rtxvulkan/shaders/shared/atrous.h>
#include <components/rtxvulkan/shaders/shared/composite.h>
#include <components/rtxvulkan/shaders/shared/shadow.h>

namespace Rtx
{
    namespace
    {
        /// The channel coming in with its variance, the channel going out, the surface (the edges in the
        /// surface, and which eye each pixel's ray left), the fill in and out — whose count the history
        /// fix reads — and the accumulator's fast means, which the fix writes into. **Sampled on the three
        /// this pass only reads**, because a twenty-five tap gather wants the texture unit's cache (a few
        /// per cent of the cascade) and it is legal from `VK_IMAGE_LAYOUT_GENERAL`.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ATROUS_BINDINGS> sBindings{
            computeBinding(Shaders::ATROUS_BIND_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_FILTERED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_SURFACE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_FILL_SOURCE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_FILL_FILTERED, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
            computeBinding(Shaders::ATROUS_BIND_FAST, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        };

        /// Both reads, because a level's inputs are sampled and its target is storage. An image
        /// is each in turn as the levels ping-pong, so a dependency that named one of the two would
        /// leave the other frame's access uncovered.
        constexpr VkAccessFlags2 sReads = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        constexpr std::array sLevelZones{ FrameZone::Filter0, FrameZone::Filter1, FrameZone::Filter2,
            FrameZone::Filter3 };
        static_assert(sLevelZones.size() == Shaders::ATROUS_LEVELS, "a zone for every level");

        /// The level's own and, after them, the composite's channels and the filters' answers, which the
        /// composing level reads as storage, as the composite does.
        constexpr std::array<VkDescriptorSetLayoutBinding, Shaders::ATROUS_COMPOSE_BINDINGS> sComposingBindings = [] {
            std::array<VkDescriptorSetLayoutBinding, Shaders::ATROUS_COMPOSE_BINDINGS> bindings{};
            for (std::uint32_t slot = 0; slot < bindings.size(); ++slot)
                bindings[slot] = slot < sBindings.size() ? sBindings[slot]
                                                         : computeBinding(slot, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
            return bindings;
        }();

        ComputePipeline<Shaders::AtrousConstants> makeLevel(
            const Device& device, bool wide, bool fill, std::string_view name)
        {
            // The masks are the composing level's alone, so every other level is made with them off.
            std::array<std::uint32_t, Shaders::ATROUS_SPEC_COUNT> specialization{};
            specialization[Shaders::ATROUS_SPEC_WIDE] = wide ? VK_TRUE : VK_FALSE;
            specialization[Shaders::ATROUS_SPEC_FILL] = fill ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::AtrousConstants>(
                device, sBindings, {}, "atrous.comp.spv", name, specialization);
        }

        ComputePipeline<Shaders::AtrousConstants> makeComposing(
            const Device& device, bool wide, const UpscaleMasks masks, std::string_view name)
        {
            std::array<std::uint32_t, Shaders::ATROUS_SPEC_COUNT> specialization{};
            specialization[Shaders::ATROUS_SPEC_WIDE] = wide ? VK_TRUE : VK_FALSE;
            // A wide level composes only as the one level of a frame with no bounce, which keeps no fill.
            specialization[Shaders::ATROUS_SPEC_FILL] = wide ? VK_FALSE : VK_TRUE;
            specialization[Shaders::ATROUS_SPEC_UPSCALE_MASKS] = masks == UpscaleMasks::Written ? VK_TRUE : VK_FALSE;
            return ComputePipeline<Shaders::AtrousConstants>(
                device, sComposingBindings, {}, "atrouscompose.comp.spv", name, specialization);
        }

        /// **One fetch of eight bytes a tap for the surface** (the normal's code and the distance) where a
        /// half-float guide and a two-float depth were two of sixteen: 3.19 ms of the cascade to 2.78 in
        /// the guild, so the octahedral `normalize` at 125 taps costs less than the bytes it saves.
        /// **The first level reads a tile in shared memory** (`atrous.comp`), each texel being reached by
        /// twenty-five pixels' taps: 0.359 to 0.295 ms in the guild. A tile for the narrow levels did not
        /// pay: what the cache answers for their repeated reads it answers without the fill and barrier.
    }

    AtrousPass::AtrousPass(const Device& device, const UpscaleMasks masks)
        : mWide(makeLevel(device, true, true, "atrous"))
        , mNarrow(makeLevel(device, false, true, "atrous-narrow"))
        , mWideNoFill(makeLevel(device, true, false, "atrous-no-fill"))
        , mComposing(makeComposing(device, false, masks, "atrous-compose"))
        , mWideComposing(makeComposing(device, true, masks, "atrous-wide-compose"))
        , mNoShadow(makeStandIn(device, toVulkanFormat(SHADOW_VISIBILITY), VK_IMAGE_USAGE_STORAGE_BIT, "no-shadow"))
    {
    }

    AtrousPass::Filtered AtrousPass::record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame, GpuTimer* timer) const
    {
        return *recordLevels(commands, images, buffer, frame, nullptr, timer);
    }

    void AtrousPass::recordComposing(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
        const GBuffer& buffer, const DenoiseFrame& frame, const Composing& composing, GpuTimer* timer) const
    {
        [[maybe_unused]] const std::optional<Filtered> answered
            = recordLevels(commands, images, buffer, frame, &composing, timer);
        assert(!answered.has_value() && "a composing cascade answered as well");
    }

    std::optional<AtrousPass::Filtered> AtrousPass::recordLevels(VkCommandBuffer commands,
        const DenoiseHistory::AccumulateImages& images, const GBuffer& buffer, const DenoiseFrame& frame,
        const Composing* composing, GpuTimer* timer) const
    {
        const Shaders::Camera& camera = frame.mSampled.mEyes.mWorld;
        assert(images.mNarrow.getWidth() >= camera.mWidth);
        assert(images.mNarrow.getHeight() >= camera.mHeight);
        assert(buffer.getWidth() >= camera.mWidth);
        assert(buffer.getHeight() >= camera.mHeight);

        // One assignment and not eight, so the filter's rays and the trace's cannot come to differ: the
        // shader rebuilds the rays with the trace's own `rayAt` from the one struct. The scratch was
        // discarded with the rest of the frame's images (`DenoiseHistory::discard`); every level after
        // the first reads what the one before wrote, which the barriers below order.
        Shaders::AtrousConstants level{
            .mEyes = frame.mSampled.mEyes,
            .mStep = 1,
            .mFixFrames = 0.0f,
            .mFrame = frame.mSampled.mFrame,
        };
        if (composing != nullptr)
        {
            level.mShadowed = (composing->mSkyShadow != nullptr ? Shaders::COMPOSITE_SHADOWED_SKY : 0u)
                | (composing->mLampShadow != nullptr ? Shaders::COMPOSITE_SHADOWED_LAMPS : 0u);
            level.mLobed = composing->mLobed ? 1u : 0u;
        }

        // The first level reads the blend and writes the mean the accumulator reads next frame —
        // SVGF's feedback; the levels after it ping-pong between the narrow image and the blend,
        // which nothing reads once the first level has, and leave the history alone. The bounce's
        // and the fill's take the same turns.
        constexpr std::size_t blended = 0;
        constexpr std::size_t history = 1;
        constexpr std::size_t narrow = 2;
        const std::array<const Image*, 3> bounce{ &images.mBlended, &images.mColour, &images.mNarrow };
        const std::array<const Image*, 3> fill{ &images.mFillBlended, &images.mFill, &images.mFillNarrow };
        std::size_t source = blended;
        std::size_t target = history;

        // **One level where no bounce is traced**: what is left to filter is a flat ambient under one
        // occlusion ray, which the first level, the accumulator's feedback and its history fix, carries
        // as well as all four (`docs/research/2026-11-05-room-ambient.md`).
        const std::uint32_t levels = frame.mSampled.mBounceRays == 0 ? 1u : Shaders::ATROUS_LEVELS;

        for (std::uint32_t pass = 0; pass < levels; ++pass)
        {
            const GpuZone timed(timer, commands, sLevelZones[pass]);

            if (pass > 0)
            {
                // The level about to run reads what the last one wrote and overwrites what it read,
                // so both channels have to be ordered against it — the second is a write after a
                // read, which needs the stages named and nothing made visible.
                Barriers between(commands);
                for (const Image* image : { bounce[source], bounce[target], fill[source], fill[target] })
                    image->addTransition(between,
                        ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | sReads },
                        ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | sReads });

                between.flush();
            }

            // Sampled from `GENERAL` on the three this pass only reads. A `SAMPLED_IMAGE`
            // descriptor names the image alone and no sampler, which is what `sBindings` declares.
            const bool composes = composing != nullptr && pass == levels - 1;
            const ComputePipeline<Shaders::AtrousConstants>& pipeline = composes
                ? (pass == 0 ? mWideComposing : mComposing)
                : (pass == 0 ? (frame.mSampled.mBounceRays == 0 ? mWideNoFill : mWide) : mNarrow);
            DescriptorWrites writes(pipeline);
            writes.image(Shaders::ATROUS_BIND_SOURCE, bounce[source]->describeSampled(VK_NULL_HANDLE));
            writes.image(Shaders::ATROUS_BIND_FILTERED, bounce[target]->describeStorage());
            writes.image(Shaders::ATROUS_BIND_SURFACE, buffer.get(Channel::Surface).describeSampled(VK_NULL_HANDLE));
            writes.image(Shaders::ATROUS_BIND_FILL_SOURCE, fill[source]->describeSampled(VK_NULL_HANDLE));
            writes.image(Shaders::ATROUS_BIND_FILL_FILTERED, fill[target]->describeStorage());
            writes.image(Shaders::ATROUS_BIND_FAST, images.mFast.describeStorage());
            if (composes)
            {
                writes.image(Shaders::ATROUS_COMPOSE_BIND_DIRECT, buffer.get(Channel::Direct).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_ALBEDO, buffer.get(Channel::Albedo).describeStorage());
                writes.image(
                    Shaders::ATROUS_COMPOSE_BIND_AMBIENT_ALBEDO, buffer.get(Channel::AmbientAlbedo).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_SHADOWED, buffer.get(Channel::Shadowed).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_LAMPED, buffer.get(Channel::Lamped).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_SHADOW,
                    (composing->mSkyShadow != nullptr ? *composing->mSkyShadow : mNoShadow).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_LAMP_SHADOW,
                    (composing->mLampShadow != nullptr ? *composing->mLampShadow : mNoShadow).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_SPECULAR, composing->mSpecular.describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_SPECULAR_ALBEDO,
                    buffer.get(Channel::SpecularAlbedo).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_PANE, composing->mPane.describeStorage());
                writes.image(
                    Shaders::ATROUS_COMPOSE_BIND_PANE_ALBEDO, buffer.get(Channel::PaneAlbedo).describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_COMPOSED, composing->mComposed.describeStorage());
                writes.image(Shaders::ATROUS_COMPOSE_BIND_STAGE_RECORD, buffer.getStageRecord().describeStorage());
                writes.image(
                    Shaders::ATROUS_COMPOSE_BIND_UPSCALE_MASKS, buffer.get(Channel::UpscaleMasks).describeStorage());
                writes.image(
                    Shaders::ATROUS_COMPOSE_BIND_LAMPED_SHARE, buffer.get(Channel::LampedShare).describeStorage());
            }

            level.mStep = 1u << pass;
            level.mFixFrames = pass == 0 && frame.mFilters.mHistoryFix ? Shaders::ACCUMULATE_FIX_FRAMES : 0.0f;

            dispatch(commands, pipeline, writes, level,
                Groups::covering(camera.mWidth, camera.mHeight, Shaders::ATROUS_WORKGROUP));

            // The next level reads what this one wrote, and writes whichever of the narrow image and
            // the blend it is not reading.
            source = target;
            target = source == narrow ? blended : narrow;
        }

        // The frame, composed into `Composing::mComposed`, is the caller's to order.
        if (composing != nullptr)
            return std::nullopt;

        // The cascade hands over what it wrote, because nothing after it does: with the last level
        // ordered against nothing, the composite ran beside the dispatch still writing it, and two
        // runs of one doll wrote different bytes over a thousand of its pixels.
        Barriers handed(commands);
        for (const Image* image : { bounce[source], fill[source] })
            image->addTransition(handed, Use::sComputeWrite,
                ImageUse{ VK_IMAGE_LAYOUT_GENERAL, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, sReads });

        handed.flush();

        // One swap past the last dispatch, so this is what that dispatch wrote.
        return Filtered{ .mIndirect = *bounce[source],
            .mFill = frame.mSampled.mBounceRays == 0 ? *bounce[source] : *fill[source] };
    }
}
