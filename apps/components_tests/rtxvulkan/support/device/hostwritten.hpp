#pragma once

#include <cstddef>
#include <span>

#include <vulkan/vulkan_core.h>

#include <components/rtxvulkan/device/commands.hpp>
#include <components/rtxvulkan/device/memory/buffer.hpp>
#include <components/rtxvulkan/device/memory/uploads.hpp>

namespace Rtx::Testing
{
    /// `write(uploads)` through a batch of `pool`, ordered and flushed here: host-written tables
    /// written the way the renderer writes them, and on the device when this returns.
    template <class Write>
    void flushWriting(CommandPool& pool, Write&& write)
    {
        Batch written(pool);
        Uploads uploads(written);
        write(uploads);
        uploads.order();
        written.flush();
    }

    /// `bytes` in a host-written buffer made for `usage`, written as the renderer writes one —
    /// through `Uploads`, over a batch of `pool` this flushes — so a kernel under test reads its input
    /// from the memory the frame's tables stand in, whichever way the build's writes travel.
    Buffer hostWritten(CommandPool& pool, std::span<const std::byte> bytes, VkBufferUsageFlags usage);

    template <class T>
    Buffer hostWritten(CommandPool& pool, std::span<const T> data, VkBufferUsageFlags usage)
    {
        return hostWritten(pool, std::as_bytes(data), usage);
    }

    /// `CommandPool::submitAndWait` for a `record` that writes host-written tables as it records, as
    /// a frame's passes do: `record(commands, uploads)`, the uploads ordered and deferred ahead of the
    /// one submit, as the renderer defers a frame's.
    template <class Record>
    void submitAndWaitWriting(CommandPool& pool, Record&& record)
    {
        Batch written(pool);
        Uploads uploads(written);
        pool.submitAndWait([&](VkCommandBuffer commands) {
            record(commands, uploads);
            uploads.order();
            written.defer();
        });
    }
}
