#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/composite.h>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

namespace Rtx
{
    class Device;
    class GBuffer;

    /// Puts the trace's channels back together into the direct channel, one frame of linear
    /// radiance: one multiply and one add, because the trace folded everything harder into the
    /// modulation term. Also owns the running sum, because a reference converges to the frame as
    /// shown, filter and all. The display curve is `TonePass`, after whatever upscales.
    ///
    /// Run only where a filter stands between the trace and the frame, or where the frame is
    /// summed: a frame with neither was composed by the trace (`VisibilityConstants::mComposed`).
    class CompositePass
    {
    public:
        CompositePass(const Device& device, const std::filesystem::path& shaderDirectory);

        /// @param buffer must have been handed over, so its writes are visible to this read, and
        ///        its direct channel made writable by compute, which is where the frame goes. Its
        ///        indirect channel is not read: `indirect` says where the bounce actually is.
        /// @param indirect the bounce to put the albedo back into — the filter's output, or the
        ///        buffer's own channel where nothing filtered it.
        /// @param sum the running total a reference is built out of, at least as large as
        ///        the frame and in `VK_IMAGE_LAYOUT_GENERAL`. Null where `mAccumulate` is zero, which
        ///        is every frame that is not building a reference.
        void record(VkCommandBuffer commands, const GBuffer& buffer, const Image& indirect, const Image* sum,
            const Shaders::CompositeConstants& constants) const;

    private:
        ComputePipeline mPipeline;

        /// What the sum's binding points at when nothing is being summed. The shader touches the
        /// sum only inside `if (mAccumulate > 0u)`, and `makeStandIn` says the rest.
        Image mNoSum;
    };
}
