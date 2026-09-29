#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "panehistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The pane filter: `CHANNEL_PANE` averaged over the frames the nearest see-through layer stood
    /// in, against a history of the layer's own — `pane.comp` says why over time alone. It runs
    /// wherever the wavelet does.
    class PanePass
    {
    public:
        PanePass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// What a frame hands the pass beside the images.
        struct Frame
        {
            const Shaders::Camera& mCamera;

            /// What a world distance is multiplied by before the history holds it,
            /// `AccumulateHistory::distanceScaleFor`.
            float mDistanceScale;

            /// True where there is no history worth carrying, as the accumulator is told.
            bool mReset;
        };

        /// Records the one dispatch and hands back the filtered light, ordered for a compute read.
        /// `buffer` must have been handed over.
        const Image& record(
            VkCommandBuffer commands, const PaneHistory::Turn& turn, const GBuffer& buffer, const Frame& frame) const;

    private:
        ComputePipeline mPipeline;
    };
}
