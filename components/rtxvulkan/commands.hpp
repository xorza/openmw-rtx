#pragma once

#include <cstddef>
#include <span>
#include <vector>

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

        /// Command buffers the caller records into again every frame.
        ///
        /// The pool allows individual reset, so re-recording one is `vkBeginCommandBuffer` and
        /// nothing else. They live as long as the pool does and are not freed individually.
        std::vector<VkCommandBuffer> allocate(std::uint32_t count);

        /// Frees every buffer this pool has handed out, and forgets what they referenced.
        ///
        /// **A recorded buffer keeps its resources alive as far as the layers are concerned**, so an
        /// image one of them blitted from cannot be destroyed while the recording still names it —
        /// which is exactly what a resize does to the frame the last present read. Every handle from
        /// `allocate` becomes invalid, and nothing may be in flight: the caller has waited.
        void reset();

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
