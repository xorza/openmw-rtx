#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "accumulatehistory.hpp"

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

        /// Blends the buffer's indirect channel with `history`, and leaves the blend and its variance
        /// (`AccumulateHistory::getBlended`) where the cascade can read them — into an image of the
        /// history's own, or `Channel::Indirect` would mean two different things.
        ///
        /// @param far the frame's far plane, which this turns into a storage scale rather than
        ///        writing a depth against. `AccumulateConstants::mDistanceScale` says why it is a
        ///        parameter of its own instead of a field of `Camera`.
        /// @param reset true where there is no history worth carrying — the first frame, a resize, a
        ///        door walked through. The same signal an upscaler is handed.
        void record(VkCommandBuffer commands, AccumulateHistory& history, const GBuffer& buffer,
            const Shaders::Camera& camera, float far, bool reset) const;

    private:
        ComputePipeline mPipeline;
    };
}
