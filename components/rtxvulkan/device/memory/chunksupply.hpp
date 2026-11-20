#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <variant>
#include <vector>

#include <vulkan/vulkan_core.h>

#include <components/rtx/common/slotledger.hpp>

#include "memory.hpp"

namespace Rtx
{
    /// A block the supply made, or the device's refusal of it.
    struct BlockMade
    {
        BlockIndex mBlock;
        std::optional<DeviceMemory> mMemory;
    };

    /// **Where the content arena's blocks are made and given back, on a thread of its own** (`docs/architecture.md`,
    /// "Device memory"): an allocation of 64 MiB is milliseconds of a driver's time on some machines, so the frame asks
    /// by name and is told by an event, and a block given back is freed off the frame as well. The one channel between
    /// the frame and the thread is the two queues here, each bounded by the blocks the ledger names.
    class ChunkSupply
    {
    public:
        /// The most requests and answers in flight: past what any arena's cap holds in blocks.
        static constexpr std::size_t sInFlightMost = 4096;

        /// @param allocator what makes and frees the blocks, which outlives this.
        explicit ChunkSupply(MemoryAllocator& allocator);

        ChunkSupply(const ChunkSupply&) = delete;
        ChunkSupply& operator=(const ChunkSupply&) = delete;

        /// Joins the thread; what was asked and not yet made is never made, and what was made and not yet taken goes
        /// back to the device.
        ~ChunkSupply();

        /// A block of `bytes` of memory type `type`, answered by a later `take`.
        void ask(BlockIndex block, std::uint32_t type, VkDeviceSize bytes);

        /// Frees `block` on the thread: nothing stands in it, and nothing in flight reads it.
        void give(DeviceMemory&& block);

        /// What was made or refused since the last take, appended to `into` in the order asked.
        void take(std::vector<BlockMade>& into);

        /// Whether nothing asked or given is still on the thread, and the wait until it is.
        bool isIdle() const;
        void awaitIdle() const;

    private:
        struct Ask
        {
            BlockIndex mBlock;
            std::uint32_t mType = 0;
            VkDeviceSize mBytes = 0;
        };

        /// A block to make, or one to free.
        using Request = std::variant<Ask, DeviceMemory>;

        void serve(const std::stop_token& stop);

        MemoryAllocator& mAllocator;

        mutable std::mutex mMutex;
        std::condition_variable_any mWake;
        mutable std::condition_variable mIdle;
        std::vector<Request> mRequests;
        std::vector<BlockMade> mMade;
        bool mBusy = false;

        /// Declared last, so it is joined before the queues it reads go.
        std::jthread mThread;
    };
}
