#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/accumulate.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The denoiser's temporal half: this frame's bounce averaged with what the same surface gave on
    /// the frames before it. SVGF, A-SVGF, ReLAX and ReBLUR are all a temporal accumulator with a
    /// cascade attached, and the cascade fills in where the accumulator was rejected rather than
    /// doing the averaging itself. It runs exactly when the wavelet does.
    class AccumulatePass
    {
    public:
        explicit AccumulatePass(const Device& device);

        /// Blends the buffer's indirect channel with the history, and leaves the blend and its variance
        /// in `images.mBlended` where the cascade can read them — an image of the history's own, or
        /// `Channel::Indirect` would mean two different things — and the fast blend in the scratch.
        /// `DenoiseHistory::discard` has readied what this reads and writes.
        void record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// Holds the blend's slow mean to the fast one (`accumulateclamp.comp`) and writes the fast
        /// means. After `record`.
        void recordClamp(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

        /// Writes the surface's history alone (`accumulatesurface.comp`), in place of `record` on a
        /// frame whose bounce nothing filters: what the shadow denoiser and the glossy filter read
        /// next frame.
        void recordSurface(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::AccumulateConstants> mPipeline;
        ComputePipeline<Shaders::AccumulateClampConstants> mClamp;
        ComputePipeline<Shaders::HistoryConstants> mSurface;
    };
}
