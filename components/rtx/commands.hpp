#ifndef OPENMW_COMPONENTS_RTX_COMMANDS_H
#define OPENMW_COMPONENTS_RTX_COMMANDS_H

#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"

namespace Rtx
{
    class Device;

    /// A command pool and the one-shot submit that setup work is made of.
    ///
    /// Setup only. Recording a frame means reusing a buffer against a fence, not allocating one and
    /// waiting for the queue to drain, and nothing here is shaped for that.
    class CommandPool
    {
    public:
        explicit CommandPool(const Device& device);
        ~CommandPool();

        CommandPool(const CommandPool&) = delete;
        CommandPool& operator=(const CommandPool&) = delete;

        /// Records `record` into a fresh command buffer, submits it, and waits for the queue.
        template <class F>
        void submitAndWait(F&& record)
        {
            const VkCommandBuffer commands = begin();
            record(commands);
            endAndWait(commands);
        }

    private:
        VkCommandBuffer begin();
        void endAndWait(VkCommandBuffer commands);

        const Device& mDevice;
        VkCommandPool mHandle = VK_NULL_HANDLE;
        VkFence mFence = VK_NULL_HANDLE;
    };

    /// A device-local buffer holding `bytes`, staged through host-visible memory.
    ///
    /// One submit per call, which is right for a load path and wrong for anything else.
    Buffer uploadBuffer(
        const Device& device, CommandPool& pool, std::span<const std::byte> bytes, VkBufferUsageFlags usage);

    template <class T>
    Buffer uploadBuffer(const Device& device, CommandPool& pool, std::span<const T> data, VkBufferUsageFlags usage)
    {
        return uploadBuffer(device, pool, std::as_bytes(data), usage);
    }
}

#endif
