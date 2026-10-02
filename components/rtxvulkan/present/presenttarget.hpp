#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/memory/image.hpp>

namespace Rtx
{
    class Device;

    /// The frame at the output extent, as two images: the picture, which the curve writes and
    /// only the next trace rewrites, and what is shown, which the interface draws over a copy of
    /// the picture and a present blits from. Two, because a frame that blended the interface into
    /// the picture left no picture without it: a frame with no trace blended the interface over the
    /// last interface, and a save's thumbnail held the menu it was saved from.
    ///
    /// **A present's blit reads what is shown long after the call returned** — it waits the acquire
    /// semaphore — and the next draw writes it: every command buffer opens with a full barrier on
    /// the one queue (`CommandPool::begin`), whose first scope is every command submitted before it,
    /// the blit included. So the next draw's first write waits for the blit on the device, and the
    /// host waits for nothing.
    class PresentTarget
    {
    public:
        /// Makes both, black and in `VK_IMAGE_LAYOUT_GENERAL`, because the interface is drawn over
        /// the picture whether or not a frame was traced into it.
        void resize(const Device& device, std::uint32_t width, std::uint32_t height);

        bool isOpen() const { return !mShown.isEmpty(); }

        /// What every frame is presented at — the one statement of the output extent, because
        /// these are the images that carry it. Zero before the first `resize`, which is what an
        /// asked extent is compared against.
        VkExtent2D getExtent() const { return VkExtent2D{ mShown.getWidth(), mShown.getHeight() }; }

        /// The picture without the interface: what the curve writes, a read back reads and the
        /// interface is drawn over.
        Image& getPicture() { return mPicture; }
        const Image& getPicture() const { return mPicture; }

        /// The picture with the interface over it: what a present blits from.
        Image& getShown() { return mShown; }
        const Image& getShown() const { return mShown; }

    private:
        Image mPicture;
        Image mShown;
    };
}
