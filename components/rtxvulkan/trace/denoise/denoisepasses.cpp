#include "denoisepasses.hpp"

#include <cassert>

#include <components/rtx/frame/reconstruction.hpp>
#include <components/rtx/renderer/channel.hpp>
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
        , mSpecular(device)
        , mPane(device)
        , mFilter(device)
    {
    }

    Denoised DenoisePasses::record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
        const Shaders::VisibilityConstants& sampled, const bool mapped, const bool lamps,
        const Reconstruction& reconstruction, GpuTimer* const timer) const
    {
        // **The shadow denoiser only where a source in the sky or a lamp can light anything.** A
        // room with no lamp has neither, and every tile of it would be classified, found to receive
        // nothing and copied through: 0.34 ms of a guild's frame, measured, for a factor of one on
        // a light of nought. With its lamps, the guild pays 0.28 ms in the pass and 0.38 ms of the
        // median frame, release, two alternated rounds: 4.24 to 4.62 ms.
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
        // **The accumulator runs on every frame the denoisers do**, so its history is fresh only
        // after a reset, which makes every history fresh: the shadow denoiser and the glossy filter,
        // which read the surface it holds, need no freshness but their own. **Its mean of the
        // bounce, the clamp and the wavelet only where the bounce is traced**: with none there is
        // nothing to filter, and the composite reads the channels of nought as the trace wrote them.
        const bool bounce = reconstruction.filtersBounce();
        TemporalFlags runs;
        runs[Temporal::Accumulate] = true;
        runs[Temporal::Bounce] = bounce;
        runs[Temporal::Shadow] = Shaders::skySourceLights(sampled) || (lamps && sampled.mNoLamps == 0u);
        runs[Temporal::Specular] = mapped;
        runs[Temporal::Pane] = true;

        const TemporalTurns::Step step = history.turn(runs);
        assert((!step.mFresh[Temporal::Accumulate]
                   || (step.mFresh[Temporal::Bounce] && step.mFresh[Temporal::Shadow] && step.mFresh[Temporal::Specular]
                       && step.mFresh[Temporal::Pane]))
            && "a fresh accumulator beside a history that is not");
        history.discard(commands, step);

        const DenoiseHistory::AccumulateImages accumulated = history.accumulate(step);
        const DenoiseFrame frame{
            .mSampled = sampled,
            .mDistanceScale = DenoiseHistory::distanceScaleFor(sampled.mFar),
            .mAntilag = reconstruction.mAntilag,
        };

        // The temporal half first: the accumulator hands on the variance of its mean, which is
        // what lets the levels below stop at an edge in the light and not only in the geometry.
        openZone(timer, commands, "accumulate");
        if (bounce)
            mAccumulate.record(commands, accumulated, buffer, frame);
        else
            mAccumulate.recordSurface(commands, accumulated, buffer, frame);
        closeZone(timer, commands);

        if (bounce)
        {
            openZone(timer, commands, "clamp");
            mAccumulate.recordClamp(commands, accumulated, buffer, frame);
            closeZone(timer, commands);
        }

        const Image* shadow = nullptr;
        if (runs[Temporal::Shadow])
        {
            openZone(timer, commands, "shadow");
            shadow = &mShadow.record(commands, history.shadow(step), buffer, frame);
            closeZone(timer, commands);
        }

        const Image* specular = &buffer.get(Channel::Specular);
        if (runs[Temporal::Specular])
        {
            openZone(timer, commands, "specular");
            specular = &mSpecular.record(commands, history.specular(step), buffer, frame);
            closeZone(timer, commands);
        }

        openZone(timer, commands, "pane");
        const Image& pane = mPane.record(commands, history.pane(step), buffer, frame);
        closeZone(timer, commands);

        if (!bounce)
            return Denoised{ .mIndirect = buffer.get(Channel::Indirect),
                .mFill = buffer.get(Channel::Fill),
                .mSpecular = *specular,
                .mPane = pane,
                .mShadow = shadow };

        // The cascade reads what the accumulator just wrote, and it reads through the texture unit
        // — so the dependency names the sampled access and not only the storage one. The history
        // the cascade writes for the next frame is ordered by the discard, which named a compute
        // write as what would come next.
        Barriers blends(commands);
        for (const Image* image : { &accumulated.mBlended, &accumulated.mFillBlended })
            image->addTransition(blends, Use::sComputeWrite, Use::sComputeReadOrSample);

        blends.flush();

        openZone(timer, commands, "filter");
        const AtrousPass::Filtered filtered = mFilter.record(commands, accumulated, buffer, frame);
        closeZone(timer, commands);

        return Denoised{ .mIndirect = filtered.mIndirect,
            .mFill = filtered.mFill,
            .mSpecular = *specular,
            .mPane = pane,
            .mShadow = shadow };
    }
}
