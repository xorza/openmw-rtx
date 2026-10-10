#include "denoisepasses.hpp"

#include <cassert>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
#include <components/rtx/renderer/framezone.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/barriers.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"
#include "temporalturns.hpp"

namespace Rtx
{
    DenoisePasses::DenoisePasses(const Device& device)
        : mAccumulate(device)
        , mShadow(device)
        , mHistoryClamp(device)
        , mSpecular(device)
        , mPane(device)
        , mFilter(device)
    {
    }

    Denoised DenoisePasses::record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
        const Shaders::VisibilityConstants& sampled, const bool mapped, const bool lamps,
        const Reconstruction& reconstruction, GpuTimer* const timer) const
    {
        // **Each field of the shadow denoiser only where its source can light anything**: the sky's
        // where the sun or a moon lights, the lamps' where a lamp does. A room with no lamp has
        // neither, and every tile of it would be classified, found to receive nothing and copied
        // through: 0.34 ms of a guild's frame, measured, for a factor of one on a light of nought.
        // With its lamps, the guild pays 0.28 ms in the pass and 0.38 ms of the median frame,
        // release, two alternated rounds: 4.24 to 4.62 ms.
        //
        // **The glossy filter only where a surface can have a lobe**, which a vanilla scene has
        // nowhere: its channel is nought, and the composite reads the channel itself for the nought
        // it adds.
        //
        // **The pane filter wherever the denoisers run**, since any frame may hold a layer — a window,
        // and every actor the game fades at the edge of its range. Where none stands it averages
        // noughts.
        //
        // A filter that does not run is fresh the next time it does, since the frames it would
        // have carried were not recorded (`TemporalTurns`).
        //
        // **The accumulator runs on every frame the denoisers do**, its mean of the bounce, the clamp
        // and the wavelet with it, so its history is fresh only after a reset, which makes every
        // history fresh: the shadow denoiser and the glossy filter, which read the surface it holds,
        // need no freshness but their own.
        TemporalFlags runs;
        runs[Temporal::Accumulate] = true;
        runs[Temporal::SkyShadow] = Shaders::skySourceLights(sampled);
        runs[Temporal::LampShadow] = lamps && sampled.mNoLamps == 0u;
        runs[Temporal::Specular] = mapped;
        runs[Temporal::Pane] = true;

        const TemporalTurns::Step step = history.turn(runs);
        assert((!step.mFresh[Temporal::Accumulate]
                   || (step.mFresh[Temporal::SkyShadow] && step.mFresh[Temporal::LampShadow]
                       && step.mFresh[Temporal::Specular] && step.mFresh[Temporal::Pane]))
            && "a fresh accumulator beside a history that is not");
        history.discard(commands, step);

        const DenoiseHistory::AccumulateImages accumulated = history.accumulate(step);
        const float distanceScale = DenoiseHistory::distanceScaleFor(sampled.mFar);
        const DenoiseFrame frame{
            .mSampled = sampled,
            .mDistanceScale = distanceScale,
            .mHeldDistanceScale = history.exchangeDistanceScale(distanceScale),
            .mFilters = reconstruction.mFilters,
        };

        // The temporal half first: the accumulator hands on the variance of its mean, which is
        // what lets the levels below stop at an edge in the light and not only in the geometry.
        mAccumulate.record(commands, accumulated, buffer, frame, timer);
        mAccumulate.recordClamp(commands, accumulated, buffer, frame, timer);

        const Image* skyShadow = nullptr;
        const Image* lampShadow = nullptr;
        if (runs[Temporal::SkyShadow] || runs[Temporal::LampShadow])
        {
            const GpuZone timed(timer, commands, FrameZone::Shadow);
            if (runs[Temporal::SkyShadow])
                skyShadow = &mShadow.record(commands, history.shadow(ShadowField::Sky, step), buffer, frame);
            if (runs[Temporal::LampShadow])
                lampShadow = &mShadow.record(commands, history.shadow(ShadowField::Lamps, step), buffer, frame);
        }

        const Image* specular = &buffer.get(Channel::Specular);
        if (runs[Temporal::Specular])
            specular = &mSpecular.record(commands, history.specular(step), buffer, frame, mHistoryClamp, timer);

        const Image& pane = mPane.record(commands, history.pane(step), buffer, frame, mHistoryClamp, timer);

        // **One dependency after the three filters and none between them**: the shadow, glossy
        // and pane passes read nothing another of them writes, so a barrier each held every one
        // back for the tail of the one before. Their answers are ordered for the composite's read.
        Barriers ready(commands);
        for (const Image* shadow : { skyShadow, lampShadow })
            if (shadow != nullptr)
                shadow->addTransition(ready, Use::sComputeWrite, Use::sComputeRead);
        if (runs[Temporal::Specular])
            specular->addTransition(ready, Use::sComputeWrite, Use::sComputeRead);
        pane.addTransition(ready, Use::sComputeWrite, Use::sComputeRead);

        // The cascade reads what the accumulator just wrote, the moments' count for the history fix
        // among it, and it reads through the texture unit — so the dependency names the sampled
        // access and not only the storage one. The history the cascade writes for the next frame
        // is ordered by the discard, which named a compute write as what would come next. In the
        // same batch as the filters' answers.
        for (const Image* image : { &accumulated.mBlended, &accumulated.mFillBlended, &accumulated.mMoments })
            image->addTransition(ready, Use::sComputeWrite, Use::sComputeReadOrSample);
        // The history fix writes its answer over the clamp's fast means, and the first level writes
        // the means the accumulator read as last frame's.
        accumulated.mFast.addTransition(ready, Use::sComputeWrite, Use::sComputeWrite);
        for (const Image* image : { &accumulated.mColour, &accumulated.mFill })
            image->addTransition(ready, Use::sComputeRead, Use::sComputeWrite);

        ready.flush();

        const AtrousPass::Filtered filtered = mFilter.record(commands, accumulated, buffer, frame, timer);

        return Denoised{ .mIndirect = filtered.mIndirect,
            .mFill = filtered.mFill,
            .mSpecular = *specular,
            .mPane = pane,
            .mSkyShadow = skyShadow,
            .mLampShadow = lampShadow };
    }
}
