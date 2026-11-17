#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"
#include "owedgrowth.hpp"

namespace Rtx
{
    class Batch;
    class Device;

    /// A buffer made again, larger, as what it holds grows. What memory it is, what the device does
    /// with it and its debug name are said once, where it is declared, and not at every growth: a
    /// table does not change memory as it grows. What a growth displaced buries itself, as every
    /// buffer does, so a frame in flight still reading it reads it to the end.
    class GrowableBuffer
    {
    public:
        /// How it grows: made again at twice what it holds as it outgrows its room, the doubling table every scene's
        /// rows are, whose next size it owes the ceiling (`OwedGrowth`); or to what its owner asks and no further, a
        /// scratch or a storage held to a room, which owes nothing past it.
        enum class Growth : std::uint8_t
        {
            Doubling,
            Exact,
        };

        /// Nothing yet, and nothing it can grow into: what a slot holds before its owner opens it.
        GrowableBuffer() = default;

        /// @param name kept, and named again on every buffer a growth makes, so it lives as long as
        ///        this does: a literal, which is what every caller passes.
        GrowableBuffer(const Device& device, BufferKind kind, VkBufferUsageFlags usage, const char* name,
            Growth growth = Growth::Doubling);

        /// Grows so it can hold `bytes`, and never leaves the buffer holding nothing: an empty one
        /// is made whatever `bytes` is. Keeps what it has where that is big enough. True where it
        /// was made again, which is a table holding nothing that the caller has to fill whole.
        bool growTo(VkDeviceSize bytes);

        /// `growTo` for a table that keeps growing: at twice what it holds where that is more, so
        /// the table is made again a logarithmic number of times rather than once per arrival.
        bool outgrow(VkDeviceSize bytes);

        /// `outgrow`, keeping what the table holds: a growth copies the old buffer into the new on
        /// the queue, through `batch`, and orders the copy ahead of the writes recorded after it, so
        /// a caller stages only what arrived. Needs `TRANSFER_SRC` and `TRANSFER_DST` in the usage
        /// (`sTableKeptUsage`).
        void outgrowKeeping(Batch& batch, VkDeviceSize bytes);

        /// Lets go of the buffer, which `batch` buries under the submit it rides, and owes nothing until it grows
        /// again: for a buffer grown past its room for one load, which the next would not need.
        void letGo(Batch& batch);

        const Buffer& get() const { return mBuffer; }
        Buffer& get() { return mBuffer; }

    private:
        const Device* mDevice = nullptr;
        BufferKind mKind = BufferKind::DeviceLocal;
        VkBufferUsageFlags mUsage = 0;
        const char* mName = "";

        Buffer mBuffer;

        /// Twice what a doubling table holds, its next growth's, where it stands in video memory (`OwedGrowth`).
        OwedGrowth mOwed;
    };
}
