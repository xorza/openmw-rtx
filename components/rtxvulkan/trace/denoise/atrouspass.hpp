#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/atrous.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;
    class Image;

    /// The denoiser: a few edge-stopping wavelet levels over the indirect channel, borrowing
    /// samples sideways from neighbours on the same surface because there is no time for enough
    /// bounces per pixel. Legitimate because the trace demodulated: this filters light, and the
    /// texture is multiplied back in afterwards. The sky, water and fog were resolved into
    /// `direct` and pass this by.
    class AtrousPass
    {
    public:
        explicit AtrousPass(const Device& device);

        /// Runs every level and returns the channel the result ended up in, because the levels
        /// alternate and a copy back would be bandwidth spent on tidiness.
        ///
        /// @param images the accumulator's: its blend of this frame's bounce, with the variance that
        ///        turns a difference in brightness into an edge or into noise, is the first level's
        ///        input; the colour history is what the first level writes and the accumulator finds as
        ///        its mean next frame, SVGF's feedback; the scratch is the other half of the ping-pong.
        /// @param buffer handed over, so the trace's writes are visible. Only the surface and the
        ///        puffs are read from it.
        /// @param frame whose two eyes the edge tests rebuild the trace's rays through.
        const Image& record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::AtrousConstants> mPipeline;
    };
}
