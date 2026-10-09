#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "blockedbuffer.hpp"
#include "buffer.hpp"
#include "frameslots.hpp"
#include "slottable.hpp"

namespace Rtx
{
    class Batch;
    class Device;

    /// Blocks every frame slot shares and blocks each slot owns, resolved through one table of block
    /// addresses a slot: the shared blocks from block nought and the slot's own from `ownFirst`. A
    /// shader that reads either kind by an id resolves it through one table and selects nothing,
    /// where an id that named its table was a select on every read. What sits in the table between
    /// the two is nought, and nothing resolves an id there.
    class JoinedBlocks
    {
    public:
        /// @param ownFirst the block a slot's own blocks start at, past any block the shared ones
        ///        can reach.
        JoinedBlocks(std::uint32_t blockSize, std::uint32_t stride, std::uint32_t ownFirst);

        void open(const Device& device, std::uint32_t slots, VkBufferUsageFlags usage, std::string_view name);

        /// Makes room for `shared` elements in the shared blocks and `own` in every slot's, and
        /// writes a slot's table again where either side made a block.
        void reserve(Batch& batch, std::uint32_t shared, std::uint32_t own);

        BlockedBuffer& getShared() { return mShared; }

        /// A slot's own blocks, as `SlotBlocks` keeps them: by element from nought, which the
        /// table resolves from `ownFirst`.
        SlotBlocks& getOwn() { return mOwn; }

        /// Where `slot`'s table is, as `BlockedBuffer::getTableAddress` says of one.
        VkDeviceAddress getTableAddress(FrameSlot slot) const { return mTables.at(slot).addressFor(); }

        VkDeviceSize getBytes() const;

    private:
        /// Writes every slot's table again, with what each side holds now.
        void join();

        const Device* mDevice = nullptr;
        std::string mTableName;

        std::uint32_t mOwnFirst;
        BlockedBuffer mShared;
        SlotBlocks mOwn;
        PerSlot<Buffer> mTables;

        /// What a table was written of: how many blocks each side held.
        std::size_t mJoinedShared = 0;
        std::size_t mJoinedOwn = 0;

        /// Cleared and refilled for each table, never freed.
        std::vector<VkDeviceAddress> mAddresses;
    };
}
