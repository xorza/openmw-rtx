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

    /// The glossy filter: `CHANNEL_SPECULAR` averaged over the frames its reflection holds still, by
    /// ReLAX's rule for how far a view may turn against a lobe — `specular.comp` says what the port
    /// keeps. It runs where the frame is denoised and the scene wears a map, which is the only place
    /// a surface has a lobe (`glossOf`).
    class SpecularPass
    {
    public:
        explicit SpecularPass(const Device& device);

        /// Records the one dispatch, whose filtered light `recordClamp` then holds to its fast means.
        /// `buffer` must have been handed over, and `DenoiseHistory::discard` has readied the images.
        void record(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images, const GBuffer& buffer,
            const DenoiseFrame& frame) const;

        /// Holds the filter's mean to its fast means by `clamp`, behind a dependency on `record`'s
        /// dispatch the caller records, and hands back the filtered light as the clamp left it: the
        /// caller orders it for a read (`DenoisePasses::record`).
        const Image& recordClamp(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame, const HistoryClampPass& clamp) const;

    private:
        ComputePipeline<Shaders::HistoryConstants> mPipeline;
    };
}
