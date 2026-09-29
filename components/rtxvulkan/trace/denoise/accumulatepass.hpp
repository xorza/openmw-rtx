#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

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
        AccumulatePass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// Blends the buffer's indirect channel with the history, and leaves the blend and its variance
        /// in `images.mBlended` where the cascade can read them — an image of the history's own, or
        /// `Channel::Indirect` would mean two different things. `DenoiseHistory::discard` has
        /// readied what this reads and writes.
        void record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

    private:
        ComputePipeline mPipeline;
    };
}
