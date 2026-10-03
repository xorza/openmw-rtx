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

    /// The denoiser: a few edge-stopping wavelet levels over the indirect channel and its fill, borrowing
    /// samples sideways from neighbours on the same surface because there is no time for enough
    /// bounces per pixel. Legitimate because the trace demodulated: this filters light, and the
    /// texture is multiplied back in afterwards. The sky, water and fog were resolved into
    /// `direct` and pass this by.
    class AtrousPass
    {
    public:
        explicit AtrousPass(const Device& device);

        /// Where the last level left the bounce and its fill.
        struct Filtered
        {
            const Image& mIndirect;
            const Image& mFill;
        };

        /// Runs every level and returns the images the result ended up in, because the levels
        /// alternate and a copy back would be bandwidth spent on tidiness.
        ///
        /// @param images the accumulator's: its blends of this frame's bounce and fill, with the
        ///        bounce's variance that turns a difference in brightness into an edge or into noise,
        ///        are the first level's input; the colour and fill histories are what the first level
        ///        writes and the accumulator finds as its means next frame, SVGF's feedback; the
        ///        scratches are the other half of the ping-pong.
        /// @param buffer handed over, so the trace's writes are visible. Only the surface and the
        ///        puffs are read from it.
        /// @param frame whose two eyes the edge tests rebuild the trace's rays through.
        Filtered record(VkCommandBuffer commands, const DenoiseHistory::AccumulateImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::AtrousConstants> mPipeline;
    };
}
