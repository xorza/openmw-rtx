#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/visibility.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "accumulatehistory.hpp"
#include "specularhistory.hpp"

namespace Rtx
{
    class Device;
    class GBuffer;

    /// The glossy filter: `CHANNEL_SPECULAR` averaged over the frames its reflection holds still, by
    /// ReLAX's rule for how far a view may turn against a lobe — `specular.comp` says what the port
    /// keeps. It runs where the wavelet does and the scene wears a map, which is the only place a
    /// surface has a lobe (`glossOf`).
    class SpecularPass
    {
    public:
        SpecularPass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// What a frame hands the pass beside the images.
        struct Frame
        {
            /// What the trace sampled: its two eyes, and the previous frame's basis and the arms'
            /// plane against it.
            const Shaders::VisibilityConstants& mSampled;

            /// The surface this frame's history belongs to, `HeldSurface`.
            HeldSurface mHeld;

            /// True where there is no history worth carrying, as the accumulator is told.
            bool mReset;
        };

        /// Records the one dispatch and hands back the filtered light, ordered for a compute read.
        /// `buffer` must have been handed over.
        const Image& record(VkCommandBuffer commands, const SpecularHistory::Turn& turn, const GBuffer& buffer,
            const Frame& frame) const;

    private:
        ComputePipeline mPipeline;
    };
}
