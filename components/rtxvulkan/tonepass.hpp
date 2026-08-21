#pragma once

#include <filesystem>

#include <vulkan/vulkan_core.h>

#include <components/rtx/shaders/tone.h>

#include "computepipeline.hpp"

namespace Rtx
{
    class Device;
    class Image;

    /// Scene-referred radiance to bytes a display understands, and nothing else.
    ///
    /// **The last pass, and the only one that knows what a display is.** Everything before it works
    /// in linear radiance — including the upscaler, which reconstructs from several frames of it —
    /// so the curve runs once, at the end, over whatever resolution the frame reached by then.
    class TonePass
    {
    public:
        TonePass(const Device& device, const std::filesystem::path& shaderDirectory);

        TonePass(const TonePass&) = delete;
        TonePass& operator=(const TonePass&) = delete;

        /// @param colour the finished frame in linear radiance, in `VK_IMAGE_LAYOUT_GENERAL`.
        /// @param target the displayable image, in `VK_IMAGE_LAYOUT_GENERAL`. Its size is what the
        ///        curve is dispatched over, so this is where an upscaled frame gets its extra
        ///        pixels encoded rather than a corner of them.
        void record(VkCommandBuffer commands, const Image& colour, const Image& target) const;

    private:
        ComputePipeline mPipeline;
    };
}
