#ifndef OPENMW_COMPONENTS_RTX_IMAGE_H
#define OPENMW_COMPONENTS_RTX_IMAGE_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "memory.hpp"

namespace Rtx
{
    class CommandPool;
    class Device;

    /// A 2D image, its allocation and its view.
    class Image
    {
    public:
        Image(
            const Device& device, std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage);
        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        VkImage getHandle() const { return mHandle; }
        VkImageView getView() const { return mView; }
        std::uint32_t getWidth() const { return mWidth; }
        std::uint32_t getHeight() const { return mHeight; }

        /// Moves the whole image to `layout`, recording into `commands`.
        void transition(VkCommandBuffer commands, VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 srcStage,
            VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) const;

        /// Copies the image to host memory. Four bytes per pixel, tightly packed, row by row.
        ///
        /// Leaves the image in `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL` rather than putting it back:
        /// everything that reads an image back is finished with it. Anything that is not must
        /// transition it again itself.
        ///
        /// Submits and waits, so it belongs to a screenshot rather than to a frame.
        std::vector<std::uint8_t> read(CommandPool& pool, VkImageLayout layout) const;

    private:
        const Device& mDevice;
        VkImage mHandle = VK_NULL_HANDLE;
        VkImageView mView = VK_NULL_HANDLE;
        DeviceMemory mMemory;
        std::uint32_t mWidth = 0;
        std::uint32_t mHeight = 0;
    };
}

#endif
