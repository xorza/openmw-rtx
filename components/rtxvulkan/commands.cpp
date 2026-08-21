#include "commands.hpp"

#include "device.hpp"
#include "result.hpp"

namespace Rtx
{
    CommandPool::CommandPool(const Device& device)
        : mDevice(device)
    {
        const VkCommandPoolCreateInfo create{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = device.getQueueFamily(),
        };
        checkVk(vkCreateCommandPool(device.getHandle(), &create, nullptr, &mHandle), "vkCreateCommandPool");

        const VkFenceCreateInfo fence{ .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        checkVk(vkCreateFence(device.getHandle(), &fence, nullptr, &mFence), "vkCreateFence");
    }

    CommandPool::~CommandPool()
    {
        if (mFence != VK_NULL_HANDLE)
            vkDestroyFence(mDevice.getHandle(), mFence, nullptr);
        if (mHandle != VK_NULL_HANDLE)
            vkDestroyCommandPool(mDevice.getHandle(), mHandle, nullptr);
    }

    std::vector<VkCommandBuffer> CommandPool::allocate(std::uint32_t count)
    {
        const VkCommandBufferAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mHandle,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = count,
        };

        std::vector<VkCommandBuffer> buffers(count);
        checkVk(vkAllocateCommandBuffers(mDevice.getHandle(), &allocate, buffers.data()), "vkAllocateCommandBuffers");
        return buffers;
    }

    VkCommandBuffer CommandPool::begin()
    {
        const VkCommandBufferAllocateInfo allocate{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = mHandle,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };

        VkCommandBuffer commands = VK_NULL_HANDLE;
        checkVk(vkAllocateCommandBuffers(mDevice.getHandle(), &allocate, &commands), "vkAllocateCommandBuffers");

        const VkCommandBufferBeginInfo begin{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        checkVk(vkBeginCommandBuffer(commands, &begin), "vkBeginCommandBuffer");

        return commands;
    }

    void CommandPool::endAndWait(VkCommandBuffer commands)
    {
        checkVk(vkEndCommandBuffer(commands), "vkEndCommandBuffer");

        const VkCommandBufferSubmitInfo buffer{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .commandBuffer = commands,
        };
        const VkSubmitInfo2 submit{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &buffer,
        };

        checkVk(vkResetFences(mDevice.getHandle(), 1, &mFence), "vkResetFences");
        checkVk(vkQueueSubmit2(mDevice.getQueue(), 1, &submit, mFence), "vkQueueSubmit2");
        checkVk(vkWaitForFences(mDevice.getHandle(), 1, &mFence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

        vkFreeCommandBuffers(mDevice.getHandle(), mHandle, 1, &commands);
    }

    Buffer uploadBuffer(
        const Device& device, CommandPool& pool, std::span<const std::byte> bytes, VkBufferUsageFlags usage)
    {
        const Buffer staging(device, bytes.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        staging.write(bytes);

        Buffer result(
            device, bytes.size(), usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        pool.submitAndWait([&](VkCommandBuffer commands) {
            const VkBufferCopy region{ .size = bytes.size() };
            vkCmdCopyBuffer(commands, staging.getHandle(), result.getHandle(), 1, &region);
        });

        return result;
    }
}
