#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include <vulkan/vulkan_core.h>

#include <components/misc/result.hpp>
#include <components/rtx/common/runs.hpp>
#include <components/rtxvulkan/device/retiring.hpp>

#include "blocklist.hpp"
#include "buffer.hpp"
#include "bufferusage.hpp"

namespace Rtx
{
    class Device;

    /// Where one bottom-level structure sits: which block of storage, and the run inside it. Empty
    /// for a mesh slot that has no structure — a slot the scene took back and has not filled.
    using StructureRoom = BlockRun;

    /// Room for bottom-level acceleration structures, as a list of buffers nothing ever moves: one
    /// buffer sized to the scene is what made a cell arriving rebuild the world. A `BlockList` over
    /// buffers, in units of the structure alignment.
    ///
    /// **A room cools before it is handed out again.** A structure gives its room back as it goes,
    /// under the stamp its handle was buried with, and the room stays taken until the timeline
    /// passes that stamp: a frame that traced the structure may still be on the queue, and a build
    /// into the same bytes would be written under it. The room is this storage's to hold and not the
    /// graveyard's, so nothing outside needs this storage to outlive it.
    class StructureStorage
    {
    public:
        /// The unit a room is handed out in: what an offset in a block has to be a multiple of.
        /// Public because a caller summing what its rooms will come to — the `least` it asks a new
        /// block for — sums them in this unit.
        static constexpr VkDeviceSize sAlignment = sStructureOffsetAlignment;

        /// @param usage what a block's buffer is created with, which is what says who may be placed
        ///        in it. What is stored this way is an opaque object the driver puts at a 256-byte
        ///        offset in a buffer the application owns.
        /// @param name what a block is called in a capture, numbered from there.
        StructureStorage(VkBufferUsageFlags usage, std::string name);

        /// Room for a structure of `bytes`, taken from the first block that has it, or why the
        /// device has none: a block is memory a mesh can be left out for — `MemoryUse::Structure`.
        ///
        /// @param least how large to make a new block where none of the existing ones can hold it:
        ///        a load asks for the whole scene's total, an arrival for nothing in particular. A
        ///        device with no room for that much makes one only as large as the structure.
        Misc::Result<StructureRoom, std::string_view> take(
            const Device& device, VkDeviceSize bytes, VkDeviceSize least);

        /// Takes a structure's room back once the timeline passes `until`, which is the stamp its
        /// handle was buried with. `AccelerationStructure`'s end, and nothing else.
        void retire(const StructureRoom& room, std::uint64_t until);

        /// Hands out again every room whose stamp is at or below `finished`, and gives a block to
        /// the device where that was the last room in it. Asked by `take`, and once a frame by the
        /// store, so a block empties without waiting for an arrival.
        void reclaim(std::uint64_t finished);

        VkBuffer getBuffer(const StructureRoom& room) const { return mBlocks.at(room.mBlock).mBuffer.getHandle(); }
        VkDeviceSize getOffset(const StructureRoom& room) const;

        /// How much storage exists, which is what the device was asked for.
        VkDeviceSize getBytes() const;

        /// What the structures standing in it occupy, beside the room asked for, because the two
        /// part company once anything is compacted.
        VkDeviceSize getLiveBytes() const;

    private:
        /// One buffer and what has been handed out inside it. The allocator has no block boundary
        /// of its own: this buffer *is* the block, and what stops a run leaving it is the capacity
        /// checked against `getEnd`.
        struct Block
        {
            Buffer mBuffer;
            RunAllocator mRuns;

            /// How long the block is in units, and nought where it has been given back.
            std::uint32_t mCapacity = 0;

            void retire()
            {
                mBuffer = Buffer();
                mCapacity = 0;
            }
        };

        /// How many places hold a block, which is what says whether this is the last one.
        std::size_t countLive() const;

        /// Makes `room` free again, and gives its block to the device where it was the last.
        void give(const StructureRoom& room);

        BlockList<Block> mBlocks;

        /// Rooms given back and not yet out of the queue's reach, in stamp order.
        Retiring<StructureRoom> mCooling;

        VkBufferUsageFlags mUsage = 0;
        std::string mName;
    };
}
