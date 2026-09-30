#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/pane.h>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;
    class Image;

    /// The pane filter: `CHANNEL_PANE` averaged over the frames the nearest see-through layer stood
    /// in, against a history of the layer's own — `pane.comp` says why over time alone. It runs
    /// wherever the wavelet does.
    class PanePass
    {
    public:
        explicit PanePass(const Device& device);

        /// Records the one dispatch and hands back the filtered light, ordered for a compute read.
        /// `buffer` must have been handed over, and `DenoiseHistory::discard` has readied the images.
        const Image& record(VkCommandBuffer commands, const DenoiseHistory::PaneImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

    private:
        ComputePipeline<Shaders::HistoryConstants> mPipeline;
    };
}
