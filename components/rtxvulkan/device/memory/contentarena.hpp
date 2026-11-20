#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <volk.h>

#include <components/rtx/common/rowvector.hpp>
#include <components/rtx/common/slotledger.hpp>
#include <components/rtxvulkan/device/owned.hpp>

#include "chunksupply.hpp"
#include "memory.hpp"

namespace Rtx
{
    class Device;

    /// **The device memory content stands in** (`docs/architecture.md`, "Device memory"): blocks of the memory types
    /// the core knows as arenas, made and freed on the supply's thread and named by the core's ledger, which says what
    /// stands where. The arena maps a block to its memory and counts what it holds; it decides nothing.
    class ContentArena
    {
    public:
        /// Reads the memory type an image of a texture and a structure's buffer stand in, which each is the lowest of
        /// video memory alone that the device places such a resource in.
        ContentArena(const Device& device, MemoryAllocator& allocator);

        ContentArena(const ContentArena&) = delete;
        ContentArena& operator=(const ContentArena&) = delete;

        /// Gives back every block it holds, through the supply.
        ~ContentArena();

        ArenaIndex arenaOf(SlotKind kind) const { return mArenaOf[static_cast<std::size_t>(kind)]; }

        /// A block of `bytes` in `arena`, answered by a later `collect`.
        void acquire(BlockIndex block, ArenaIndex arena, VkDeviceSize bytes);

        /// Gives `block` back, which nothing stands in.
        void release(BlockIndex block);

        /// What the supply made or refused since the last call, into `arrived` and `refused`.
        void collect(std::vector<BlockIndex>& arrived, std::vector<BlockIndex>& refused);

        /// The memory `block` is, which has arrived.
        const DeviceMemory& getBlock(BlockIndex block) const;

        /// The buffer over the whole of `block`, which has arrived, where its arena stands structures: what a
        /// structure's slot is a range of, at the slot's offset.
        VkBuffer getBuffer(BlockIndex block) const;

        /// What the arena's blocks come to, whole: the `A` of the arena's cap.
        VkDeviceSize getBytes() const { return mBytes; }

        /// **The arena's cap** (`docs/architecture.md`, "The arena's cap"): what `memory`'s budget leaves of everything
        /// the process holds outside the arena, as it stands, less what that owes and the chunk kept for what moves.
        /// Read once a frame, after `MemoryAllocator::refreshBudget`.
        VkDeviceSize capUnder(const MemoryAllocator& memory) const;

        /// Whether nothing asked or given back is on the supply's thread, and the wait until it is: what a run that
        /// compares frames waits on (`ResidencyDevice::awaitBlocks`).
        bool isIdle() const { return mSupply.isIdle(); }
        void awaitIdle() const { mSupply.awaitIdle(); }

    private:
        /// One block that arrived: its memory, and where its arena stands structures, a buffer over all of it, made as
        /// the block arrives and let go of as it goes back.
        struct Block
        {
            DeviceMemory mMemory;
            Owned<VkBuffer, vkDestroyBuffer> mBuffer;
        };

        const Device& mDevice;
        ChunkSupply mSupply;
        std::array<ArenaIndex, sSlotKindCount> mArenaOf{};
        RowVector<std::optional<Block>, BlockIndex> mBlocks;

        /// The arena each block was asked in.
        RowVector<ArenaIndex, BlockIndex> mBlockArenas;
        std::vector<BlockMade> mMade;
        VkDeviceSize mBytes = 0;
    };
}
