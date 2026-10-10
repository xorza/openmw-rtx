#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The denoiser's temporal half: this frame's bounce averaged with what the same surface gave on
    /// the frames before it. SVGF, A-SVGF, ReLAX and ReBLUR are all a temporal accumulator with a
    /// cascade attached, and the cascade fills in where the accumulator was rejected rather than
    /// doing the averaging itself. It runs on every frame the denoisers do, as the wavelet does.
    class AccumulatePass
    {
    public:
        explicit AccumulatePass(const Device& device);

        /// Blends the buffer's indirect channel with the history, and leaves the blend in
        /// `images.mBlended` where the cascade can read it — an image of the history's own, or
        /// `Channel::Indirect` would mean two different things — its moments beside it, and the fast
        /// blend in the scratch. `DenoiseHistory::discard` has readied what this reads and writes.
        void record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// Holds the blend's slow mean to the fast one (`accumulateclamp.comp`), writes its variance
        /// beside it and writes the fast means. After `record`, behind a dependency on everything it
        /// wrote and read, which the caller records (`DenoisePasses::record`).
        void recordClamp(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::AccumulateConstants> mPipeline;
        ComputePipeline<Shaders::AccumulateClampConstants> mClamp;
        ComputePipeline<Shaders::AccumulateClampConstants> mClampRing;
    };
}
