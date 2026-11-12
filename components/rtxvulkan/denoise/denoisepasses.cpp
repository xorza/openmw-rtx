#include "denoisepasses.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/frame/gbuffer.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"
#include "temporalturns.hpp"

namespace Rtx
{
    namespace
    {
        /// Orders every dispatch of one stage against every dispatch of the next, over every image
        /// either reads or writes: one dependency for every family of the stage, since every image a
        /// denoiser touches stands in `GENERAL` and no family reads what another writes.
        void orderStage(VkCommandBuffer commands)
        {
            handOver(commands,
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT },
                BufferUse{ VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                    VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
                        | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT });
        }
    }

    const Image& DenoisePasses::clampLayer(VkCommandBuffer commands, const TemporalLayerPass& layer,
        const DenoiseHistory::LayerImages& images, const GBuffer& buffer, const DenoiseFrame& frame) const
    {
        mHistoryClamp.record(commands,
            HistoryClampPass::Images{ .mSampled = buffer.get(layer.getRow().mSampled),
                .mMean = images.mMean,
                .mFastBlended = images.mFastBlended,
                .mFast = images.mFast },
            Shaders::HistoryClampConstants{ .mWidth = frame.mSampled.mEyes.mWorld.mWidth,
                .mHeight = frame.mSampled.mEyes.mWorld.mHeight,
                .mAntilag = frame.mFilters.mAntilag ? 1u : 0u,
                .mFrame = frame.mSampled.mFrame,
                .mLayer = layer.getRow().mClampLayer });
        return images.mMean;
    }

    DenoisePasses::DenoisePasses(const Device& device, const UpscaleMasks masks)
        : mAccumulate(device)
        , mShadow(device)
        , mHistoryClamp(device)
        , mSpecular(device, TemporalLayer::Glossy)
        , mPane(device, TemporalLayer::Pane)
        , mFilter(device, masks)
    {
    }

    Denoised DenoisePasses::record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
        const Request& request, GpuTimer* const timer) const
    {
        return *recordAll(commands, history, buffer, request, nullptr, timer);
    }

    void DenoisePasses::recordComposing(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
        const Request& request, const Image& composed, GpuTimer* const timer) const
    {
        [[maybe_unused]] const std::optional<Denoised> answered
            = recordAll(commands, history, buffer, request, &composed, timer);
        assert(!answered.has_value() && "a composing cascade answered as well");
    }

    std::optional<Denoised> DenoisePasses::recordAll(VkCommandBuffer commands, DenoiseHistory& history,
        const GBuffer& buffer, const Request& request, const Image* const composed, GpuTimer* const timer) const
    {
        const Shaders::VisibilityConstants& sampled = request.mSampled;
        const bool mapped = request.mMapped;
        const bool lamps = request.mLamps;
        const Reconstruction& reconstruction = request.mReconstruction;
        // **Each field of the shadow denoiser only where its source can light anything**: the sky's where
        // the sun or a moon lights, the lamps' where a lamp does. A room with no lamp has neither, and
        // every tile of it would be classified, found to receive nothing and copied through: 0.34 ms of a
        // guild's frame for a factor of one on a light of nought.
        //
        // **The glossy filter only where a surface can have a lobe**, which a vanilla scene has nowhere:
        // its channel is nought, and the composite reads the channel itself for the nought it adds.
        //
        // **The pane filter wherever the denoisers run**, since any frame may hold a layer — a window,
        // and every actor the game fades at the edge of its range. Where none stands it averages noughts.
        //
        // A filter that does not run is fresh the next time it does, since the frames it would have
        // carried were not recorded (`TemporalTurns`). **The accumulator runs on every frame the
        // denoisers do**, so its history is fresh only after a reset, which makes every history fresh:
        // the shadow denoiser and the glossy filter, which read the surface it holds, need no freshness
        // but their own.
        TemporalFlags runs;
        runs[Temporal::Accumulate] = true;
        runs[Temporal::SkyShadow] = Shaders::skySourceLights(sampled);
        runs[Temporal::LampShadow] = lamps && sampled.mNoLamps == 0u;
        runs[Temporal::Specular] = mapped;
        runs[Temporal::Pane] = true;
        runs[Temporal::Fill] = sampled.mBounceRays != 0;

        const TemporalTurns::Step step = history.turn(runs, sampled.mFrame);
        if (step.mFresh[Temporal::Accumulate])
            for ([[maybe_unused]] const Temporal other :
                { Temporal::SkyShadow, Temporal::LampShadow, Temporal::Specular, Temporal::Pane, Temporal::Fill })
                assert(step.mFresh[other] && "a fresh accumulator beside a history that is not");
        history.discard(commands, step);

        const DenoiseHistory::AccumulateImages accumulated = history.accumulate(step);
        const DenoiseFrame frame{
            .mSampled = sampled,
            .mFilters = reconstruction.mFilters,
            .mWrittenBefore = step.mWrittenBefore,
        };

        // **Stage by stage, and every family's dispatch of a stage behind one barrier**: the accumulator,
        // the two shadow fields, the glossy and the pane filters read nothing another of them writes, so
        // recording one family after the last held every dispatch back for the tail of every one before
        // it — thirteen drains where six do. The temporal passes first: the accumulator hands on the
        // variance of its mean, which lets the levels below stop at an edge in the light and not only in
        // the geometry.
        std::array<const DenoiseHistory::ShadowImages*, sShadowFields> fields{};
        std::size_t running = 0;
        const DenoiseHistory::ShadowImages sky = history.shadow(ShadowField::Sky, step);
        const DenoiseHistory::ShadowImages lamped = history.shadow(ShadowField::Lamps, step);
        if (runs[Temporal::SkyShadow])
            fields[running++] = &sky;
        if (runs[Temporal::LampShadow])
            fields[running++] = &lamped;
        const std::span<const DenoiseHistory::ShadowImages* const> shadows(fields.data(), running);

        const DenoiseHistory::LayerImages glossy = history.specular(step);
        const DenoiseHistory::LayerImages layers = history.pane(step);
        {
            const GpuZone timed(timer, commands, FrameZone::Temporal);
            mAccumulate.record(commands, accumulated, buffer, frame);
            for (const DenoiseHistory::ShadowImages* field : shadows)
                mShadow.recordMask(commands, *field, buffer, frame);
            if (runs[Temporal::Specular])
                mSpecular.record(commands, glossy, buffer, frame);
            mPane.record(commands, layers, buffer, frame);
        }
        orderStage(commands);

        const Image* specular = &buffer.get(Channel::Specular);
        const Image* pane = nullptr;
        {
            const GpuZone timed(timer, commands, FrameZone::Clamp);
            mAccumulate.recordClamp(commands, accumulated, buffer, frame);
            for (const DenoiseHistory::ShadowImages* field : shadows)
                mShadow.recordTiles(commands, *field, buffer, frame);
            if (runs[Temporal::Specular])
                specular = &clampLayer(commands, mSpecular, glossy, buffer, frame);
            pane = &clampLayer(commands, mPane, layers, buffer, frame);
        }

        const Image* skyShadow = runs[Temporal::SkyShadow] ? &sky.mVisibility : nullptr;
        const Image* lampShadow = runs[Temporal::LampShadow] ? &lamped.mVisibility : nullptr;
        if (!shadows.empty())
        {
            const GpuZone timed(timer, commands, FrameZone::Shadow);
            const std::uint32_t filtering = (runs[Temporal::SkyShadow] ? 1u << Shaders::SHADOW_FIELD_SKY : 0u)
                | (runs[Temporal::LampShadow] ? 1u << Shaders::SHADOW_FIELD_LAMPS : 0u);
            for (std::uint32_t level = 0; level < Shaders::SHADOW_FILTER_LEVELS; ++level)
            {
                orderStage(commands);
                mShadow.recordLevel(commands, level, { &sky, &lamped }, filtering, buffer, frame);
            }
        }

        // The filters' answers, ordered for the read of whatever composes the frame: the wavelet's
        // last level, or the composite.
        Barriers ready(commands);
        for (const Image* shadow : { skyShadow, lampShadow })
            if (shadow != nullptr)
                shadow->addTransition(ready, Use::sComputeWrite, Use::sComputeRead);
        if (runs[Temporal::Specular])
            specular->addTransition(ready, Use::sComputeWrite, Use::sComputeRead);
        pane->addTransition(ready, Use::sComputeWrite, Use::sComputeRead);

        // The cascade reads what the accumulator just wrote (the history fix's frame count in the fill's
        // alpha among it) through the texture unit, so the dependency names the sampled access and not
        // only the storage one. The history the cascade writes for the next frame is ordered by the
        // discard, which named a compute write as what would come next. In the same batch as the answers.
        for (const Image* image : { &accumulated.mBlended, &accumulated.mFillBlended })
            image->addTransition(ready, Use::sComputeWrite, Use::sComputeReadOrSample);
        // The history fix writes its answer over the clamp's fast means, and the first level writes
        // the means the accumulator read as last frame's.
        accumulated.mFast.addTransition(ready, Use::sComputeWrite, Use::sComputeWrite);
        for (const Image* image : { &accumulated.mColour, &accumulated.mFill })
            image->addTransition(ready, Use::sComputeRead, Use::sComputeWrite);
        ready.flush();

        if (composed != nullptr)
        {
            mFilter.recordComposing(commands, accumulated, buffer, frame,
                AtrousPass::Composing{ .mComposed = *composed,
                    .mSpecular = *specular,
                    .mPane = *pane,
                    .mSkyShadow = skyShadow,
                    .mLampShadow = lampShadow,
                    .mLobed = mapped },
                timer);
            return std::nullopt;
        }

        const AtrousPass::Filtered filtered = mFilter.record(commands, accumulated, buffer, frame, timer);
        return Denoised{ .mIndirect = filtered.mIndirect,
            .mFill = filtered.mFill,
            .mSpecular = *specular,
            .mPane = *pane,
            .mSkyShadow = skyShadow,
            .mLampShadow = lampShadow };
    }
}
