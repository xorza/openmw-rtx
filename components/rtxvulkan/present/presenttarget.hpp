#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Device;

    /// The frame as bytes at the output extent, which is what anything outside the renderer reads
    /// and what a present blits from. One image, though a present's blit reads it long after the
    /// call returned — it waits the acquire semaphore — and the next frame writes it: every command
    /// buffer opens with a full barrier on the one queue (`CommandPool::begin`), whose first scope
    /// is every command submitted before it, the blit included. So the next frame's first write
    /// waits for the blit on the device, and the host waits for nothing.
    class PresentTarget
    {
    public:
        /// Makes it, black and in `VK_IMAGE_LAYOUT_GENERAL`, because the GUI is drawn over it
        /// whether or not a frame was traced into it.
        void resize(const Device& device, std::uint32_t width, std::uint32_t height);

        bool isOpen() const { return !mImage.isEmpty(); }

        /// What every frame is presented at — the one statement of the output extent, because
        /// this is the image that carries it. Zero before the first `resize`, which is what an
        /// asked extent is compared against.
        VkExtent2D getExtent() const { return VkExtent2D{ mImage.getWidth(), mImage.getHeight() }; }

        Image& get() { return mImage; }
        const Image& get() const { return mImage; }

    private:
        Image mImage;
    };
}
