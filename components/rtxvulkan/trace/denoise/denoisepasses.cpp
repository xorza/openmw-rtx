#include "denoisepasses.hpp"

#include <cassert>

#include <components/rtx/renderer/frameimage.hpp>
#include <components/rtxvulkan/device/gputimer.hpp>
#include <components/rtxvulkan/device/memory/imageuse.hpp>
#include <components/rtxvulkan/trace/gbuffer.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"
#include "temporalturns.hpp"

namespace Rtx
{
    DenoisePasses::DenoisePasses(const Device& device, const std::filesystem::path& shaderDirectory)
        : mAccumulate(device, shaderDirectory)
        , mShadow(device, shaderDirectory)
        , mSpecular(device, shaderDirectory)
        , mPane(device, shaderDirectory)
        , mFilter(device, shaderDirectory)
    {
    }

    Denoised DenoisePasses::record(VkCommandBuffer commands, DenoiseHistory& history, const GBuffer& buffer,
        const Shaders::VisibilityConstants& sampled, const bool mapped, GpuTimer* const timer) const
    {
        // **The shadow denoiser only where a source in the sky lights anything.** A room has none,
        // and every tile of it would be classified, found to receive nothing and copied through:
        // 0.34 ms of a guild's frame, measured, for a factor of one on a light of nought.
        //
        // **The glossy filter only where a surface can have a lobe**, which a vanilla scene has
        // nowhere: its channel is nought, and the composite reads the channel itself for the nought
        // it adds.
        //
        // **The pane filter wherever the wavelet runs**, since any frame may hold a layer — a window,
        // and every actor the game fades at the edge of its range. Where none stands it averages
        // noughts.
        //
        // A filter that does not run is fresh the next time it does, since the frames it would
        // have carried were not recorded (`TemporalTurns`).
        //
        // **The accumulator runs on every frame the denoisers do**, so its history is fresh only
        // after a reset, which makes every history fresh: the shadow denoiser and the glossy filter,
        // which read the surface it holds, need no freshness but their own.
        TemporalFlags runs;
        runs[Temporal::Accumulate] = true;
        runs[Temporal::Shadow] = Shaders::skySourceLights(sampled);
        runs[Temporal::Specular] = mapped;
        runs[Temporal::Pane] = true;

        const TemporalTurns::Step step = history.turn(runs);
        assert((!step.mFresh[Temporal::Accumulate]
                   || (step.mFresh[Temporal::Shadow] && step.mFresh[Temporal::Specular] && step.mFresh[Temporal::Pane]))
            && "a fresh accumulator beside a history that is not");
        history.discard(commands, step);

        const DenoiseHistory::AccumulateImages accumulated = history.accumulate(step);
        const DenoiseFrame frame{
            .mSampled = sampled,
            .mDistanceScale = DenoiseHistory::distanceScaleFor(sampled.mFar),
        };

        // The temporal half first: the accumulator hands on the variance of its mean, which is
        // what lets the levels below stop at an edge in the light and not only in the geometry.
        openZone(timer, commands, "accumulate");
        mAccumulate.record(commands, accumulated, buffer, frame);
        closeZone(timer, commands);

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

        // The cascade reads what the accumulator just wrote, and it reads through the texture unit
        // — so the dependency names the sampled access and not only the storage one. The history
        // the cascade writes for the next frame is ordered by the discard, which named a compute
        // write as what would come next.
        accumulated.mBlended.transition(commands, Use::sComputeWrite, Use::sComputeReadOrSample);

        openZone(timer, commands, "filter");
        const Image& indirect = mFilter.record(commands, accumulated, buffer, frame);
        closeZone(timer, commands);

        return Denoised{ .mIndirect = indirect, .mSpecular = *specular, .mPane = pane, .mShadow = shadow };
    }
}
