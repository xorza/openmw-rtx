#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/camera.h>
#include <components/rtxvulkan/device/handles.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/frameslots.hpp>
#include <components/rtxvulkan/device/memory/image.hpp>
#include <components/rtxvulkan/pipeline/computepipeline.hpp>

#include "fsrframe.hpp"

namespace Rtx
{
    class Device;

    /// Everything one reconstruction reads. Every image is created with `VK_IMAGE_USAGE_SAMPLED_BIT`,
    /// since the upscaler samples all of them.
    struct UpscaleInputs
    {
        /// The frame at render resolution, as the trace chain composed it: denoised where the
        /// denoisers ran, and the trace's own composite where nothing filtered it.
        const Image& mColour;

        /// `CHANNEL_SURFACE`: the distance from the eye along each pixel's ray in `g`, which the
        /// upscaler's depth is worked out from (`fsrcallbacks.glsl`).
        const Image& mSurface;

        /// Where each surface stood on the previous frame's screen, less where it was sampled on this
        /// one, in render pixels.
        const Image& mMotion;

        /// `CHANNEL_PUFFS`, for the one bit that says which eye a pixel's ray left.
        const Image& mPuffs;

        /// `CHANNEL_UPSCALE_MASKS`: the reactive mask and the transparency and composition mask, as
        /// the trace measured what `mMotion` does not describe.
        const Image& mMasks;

        /// The two eyes the trace sampled through, jitter and all.
        Shaders::Camera mCamera;
        Shaders::Camera mArms;

        /// How long since the previous frame, in milliseconds, or nought where there was none.
        float mFrameDeltaMs = 0.0f;

        /// Whether the previous frame is worth anything. True after a jump no motion vector can
        /// describe: a new cell, a teleport, the first frame after a resize.
        bool mReset = false;

        /// The frame in flight this reconstruction is recorded into, whose constants it writes.
        FrameSlot mSlot;
    };

    /// FSR 3.1.4, ported: AMD's temporal upscaler, which also reconstructs a frame at its own size,
    /// as the anti-aliasing. The passes are AMD's (`extern/fidelityfx/`), the bindings and the
    /// inputs the port's (`shaders/upscale/`), and this is the SDK host's dispatch
    /// (`ffx_fsr3upscaler.cpp`): the resources, the order, the clears. `FsrFrame` is its constants.
    ///
    /// **Compute shaders and nothing else**, so every device that runs the renderer runs it: no
    /// extension, no library, no vendor.
    class Upscaler
    {
    public:
        Upscaler(const Device& device, const std::filesystem::path& shaderDirectory);
        ~Upscaler();

        Upscaler(const Upscaler&) = delete;
        Upscaler& operator=(const Upscaler&) = delete;

        /// Builds what the upscaler keeps for one pair of extents and the image it writes, releasing
        /// the last, and clears all of it: the frame after starts a history of its own. Once per
        /// resolution, and never per frame. The caller has waited for anything still reading the old.
        void resize(VkExtent2D render, VkExtent2D output);

        /// Lets what `resize` built go and keeps the pipelines, for a mode turned off that may come back.
        void release();

        /// The image `record` writes, which the display composites the puffs over and maps.
        const Image& getOutput() const;

        /// Records one reconstruction into `getOutput`, at the output extent, and leaves it as
        /// `Use::sAnyGeneralWrite`. After `resize`.
        void record(VkCommandBuffer commands, const UpscaleInputs& inputs);

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
        static constexpr std::size_t sPasses = 7;

    private:
        struct Targets;

        const Device& mDevice;

        std::array<ComputePipeline, sPasses> mPipelines;
        Sampler mSampler;

        /// The three constant blocks, one buffer a frame in flight: `FSR_BLOCKS_BYTES` written whole
        /// by the host before the frame's dispatches read it.
        PerSlot<Buffer> mBlocks;

        FsrFrame mFrame;

        /// Empty until `resize`, and after `release`.
        std::unique_ptr<Targets> mTargets;
    };
}
