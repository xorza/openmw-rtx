#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/frame/upscale.hpp>
#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/device/memory/imagearena.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>
#include <components/rtxvulkan/pipeline/pipeline.hpp>

#include "fsrframe.hpp"
#include "upscaleinputs.hpp"

namespace Rtx
{
    class Device;
    class GpuTimer;

    /// FSR 3.1.4, ported: AMD's temporal upscaler, which also reconstructs a frame at its own size,
    /// as the anti-aliasing. The passes are AMD's (`extern/fidelityfx/`), the bindings and the
    /// inputs the port's (`shaders/upscale/`), and this is the SDK host's dispatch
    /// (`ffx_fsr3upscaler.cpp`): the resources, the order, the clears. `FsrFrame` is its constants.
    ///
    /// **Compute shaders and nothing else**, so every device that runs the renderer runs it: no
    /// extension, no library, no vendor.
    class FsrUpscaler
    {
    public:
        explicit FsrUpscaler(const Device& device);
        ~FsrUpscaler();

        FsrUpscaler(const FsrUpscaler&) = delete;
        FsrUpscaler& operator=(const FsrUpscaler&) = delete;

        /// Builds what the upscaler keeps for one pair of extents, and clears all of it: the frame
        /// after starts a history of its own. Once per resolution, and never per frame. Takes
        /// `shared`, the images `declareShared` declared at these extents as the frame's arena placed
        /// them, whether the extents moved or not; targets that stand are asked at their own extents,
        /// and let go of first where they moved. The mode is DLSS's to read: FSR's arithmetic follows
        /// from the extents alone.
        void resize(Upscale mode, VkExtent2D render, VkExtent2D output, const ArenaRun& shared);

        /// The images a reconstruction at these extents writes whole before it reads them and no
        /// later frame reads, which the frame's arena places (`ImageArena`), appended to `into`: in
        /// use over the upscale alone (`FrameStep::Upscale`), and the image it writes until the
        /// display has read it.
        static void declareShared(VkExtent2D render, VkExtent2D output, std::vector<ArenaImage>& into);

        /// How many images `declareShared` declares.
        static std::size_t getSharedCount();

        /// Lets what `resize` built go and keeps the pipelines, for a mode turned off that may come back.
        void release();

        /// Whether `resize` built anything that stands.
        bool isBuilt() const { return mTargets != nullptr; }

        /// Whether what `resize` built is for these extents, which a resize to them keeps.
        bool isAt(VkExtent2D render, VkExtent2D output) const;

        /// What `resize` to these extents builds, in the device's memory, and not what the frame's
        /// arena holds of it.
        static VkDeviceSize bytesAt(const Device& device, VkExtent2D render, VkExtent2D output);

        /// Says the history is worthless, until the next `record`: after a jump no motion vector can
        /// describe, a new cell or a teleport. A `resize` says it too.
        void reset() { mFrame.reset(); }

        /// Whether the next `record` reads no history.
        bool isFresh() const { return mFrame.isFresh(); }

        /// Records one reconstruction at the output extent, and hands over the image it wrote,
        /// which the display composites the puffs over and maps. After `resize`. The frame's
        /// constants are written in queue order ahead of the passes.
        HandedImage record(VkCommandBuffer commands, const UpscaleInputs& inputs, GpuTimer* timer);

        /// One line for `info`: which upscaler this renderer has.
        static std::string_view describe() { return "FSR 3.1.4, ported"; }

        /// What the passes are, in the order `record` dispatches them.
        enum class Pass : std::uint8_t
        {
            Inputs,
            LumaPyramid,
            ChangePyramid,
            Change,
            Reactivity,
            Instability,
            Accumulate,
        };
        static constexpr std::size_t sPasses = static_cast<std::size_t>(Pass::Accumulate) + 1;

    private:
        struct Targets;

        const Device& mDevice;

        std::array<ComputePipeline<NoConstants>, sPasses> mPipelines;
        Sampler mSampler;

        /// The three constant blocks, `FSR_BLOCKS_BYTES` written whole in queue order ahead of the
        /// frame's dispatches (`Buffer::updateInline`), so one buffer serves every frame, as the
        /// visibility pass's constants do.
        Buffer mBlocks;

        FsrFrame mFrame;

        /// Empty until `resize`, and after `release`.
        std::unique_ptr<Targets> mTargets;
    };
}
