#include "image.hpp"

#include <cassert>
#include <cstring>

#include "buffer.hpp"
#include "commands.hpp"
#include "device.hpp"
#include "result.hpp"

namespace Rtx
{
    Image::Image(
        const Device& device, std::uint32_t width, std::uint32_t height, VkFormat format, VkImageUsageFlags usage)
        : mDevice(device)
        , mWidth(width)
        , mHeight(height)
    {
        const VkImageCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = { width, height, 1 },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        checkVk(vkCreateImage(device.getHandle(), &create, nullptr, &mHandle), "vkCreateImage");

        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device.getHandle(), mHandle, &requirements);
        mMemory = DeviceMemory(
            device, requirements.size, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false);
        checkVk(vkBindImageMemory(device.getHandle(), mHandle, mMemory.getHandle(), 0), "vkBindImageMemory");

        const VkImageViewCreateInfo view{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = mHandle,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };
        checkVk(vkCreateImageView(device.getHandle(), &view, nullptr, &mView), "vkCreateImageView");

        device.setName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(mHandle), "target");
        device.setName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(mView), "target");
    }

    Image::~Image()
    {
        if (mView != VK_NULL_HANDLE)
            vkDestroyImageView(mDevice.getHandle(), mView, nullptr);
        if (mHandle != VK_NULL_HANDLE)
            vkDestroyImage(mDevice.getHandle(), mHandle, nullptr);
    }

    void Image::transition(VkCommandBuffer commands, VkImageLayout from, VkImageLayout to,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage,
        VkAccessFlags2 dstAccess) const
    {
        const VkImageMemoryBarrier2 barrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = srcStage,
            .srcAccessMask = srcAccess,
            .dstStageMask = dstStage,
            .dstAccessMask = dstAccess,
            .oldLayout = from,
            .newLayout = to,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = mHandle,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 },
        };

        const VkDependencyInfo dependency{
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .imageMemoryBarrierCount = 1,
            .pImageMemoryBarriers = &barrier,
        };
        vkCmdPipelineBarrier2(commands, &dependency);
    }

    std::vector<std::uint8_t> Image::read(CommandPool& pool, VkImageLayout layout) const
    {
        const VkDeviceSize bytes = VkDeviceSize{ mWidth } * mHeight * 4;
        const Buffer staging(mDevice, bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        pool.submitAndWait([&](VkCommandBuffer commands) {
            transition(commands, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);

            const VkBufferImageCopy region{
                .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 },
                .imageExtent = { mWidth, mHeight, 1 },
            };
            vkCmdCopyImageToBuffer(
                commands, mHandle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.getHandle(), 1, &region);
        });

        std::vector<std::uint8_t> pixels(bytes);
        const void* mapped = staging.map();
        std::memcpy(pixels.data(), mapped, bytes);
        staging.unmap();
        return pixels;
    }
}
