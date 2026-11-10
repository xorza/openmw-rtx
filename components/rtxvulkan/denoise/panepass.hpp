#pragma once

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/shaders/shared/accumulate.h>

#include "denoiseframe.hpp"
#include "denoisehistory.hpp"
#include "historyclamppass.hpp"

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

        /// Records the one dispatch, whose filtered light `recordClamp` then holds to its fast means.
        /// `buffer` must have been handed over, and `DenoiseHistory::discard` has readied the images.
        void record(VkCommandBuffer commands, const DenoiseHistory::PaneImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// Holds the filter's mean to its fast means by `clamp`, behind a dependency on `record`'s
        /// dispatch the caller records, and hands back the filtered light as the clamp left it: the
        /// caller orders it for a read (`DenoisePasses::record`).
        const Image& recordClamp(VkCommandBuffer commands, const DenoiseHistory::PaneImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame, const HistoryClampPass& clamp) const;

    private:
        ComputePipeline<Shaders::HistoryConstants> mPipeline;
    };
}
