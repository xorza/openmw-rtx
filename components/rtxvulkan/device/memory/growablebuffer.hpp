#pragma once

#include <string_view>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"

namespace Rtx
{
    class Device;

    /// A buffer made again, larger, as what it holds grows. What memory it is, what the device does
    /// with it and its debug name are said once, where it is declared, and not at every growth: a
    /// table does not change memory as it grows. What a growth displaced buries itself, as every
    /// buffer does, so a frame in flight still reading it reads it to the end.
    class GrowableBuffer
    {
    public:
        /// Nothing yet, and nothing it can grow into: what a slot holds before its owner opens it.
        GrowableBuffer() = default;

        /// @param name a literal, which is what every caller passes and all a debug name is asked
        ///        to be.
        GrowableBuffer(const Device& device, BufferKind kind, VkBufferUsageFlags usage, std::string_view name)
            : mDevice(&device)
            , mKind(kind)
            , mUsage(usage)
            , mName(name)
        {
        }

        /// Grows so it can hold `bytes`, and never leaves the buffer holding nothing: an empty one
        /// is made whatever `bytes` is. Keeps what it has where that is big enough. True where it
        /// was made again, which is a table holding nothing that the caller has to fill whole.
        bool growTo(VkDeviceSize bytes);

        /// `growTo` for a table that keeps growing: at twice what it holds where that is more, so
        /// the table is made again a logarithmic number of times rather than once per arrival.
        bool outgrow(VkDeviceSize bytes);

        const Buffer& get() const { return mBuffer; }
        Buffer& get() { return mBuffer; }

    private:
        const Device* mDevice = nullptr;
        BufferKind mKind = BufferKind::DeviceLocal;
        VkBufferUsageFlags mUsage = 0;
        std::string_view mName;

        Buffer mBuffer;
    };
}
