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
    class Image;

    /// The glossy filter: `CHANNEL_SPECULAR` averaged over the frames its reflection holds still, by
    /// ReLAX's rule for how far a view may turn against a lobe — `specular.comp` says what the port
    /// keeps. It runs where the wavelet does and the scene wears a map, which is the only place a
    /// surface has a lobe (`glossOf`).
    class SpecularPass
    {
    public:
        SpecularPass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// Records the one dispatch and hands back the filtered light, ordered for a compute read.
        /// `buffer` must have been handed over, and `DenoiseHistory::discard` has readied the images.
        const Image& record(VkCommandBuffer commands, const DenoiseHistory::SpecularImages& images,
            const GBuffer& buffer, const DenoiseFrame& frame) const;

    private:
        ComputePipeline mPipeline;
    };
}
