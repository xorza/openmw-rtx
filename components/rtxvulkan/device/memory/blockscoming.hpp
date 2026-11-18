#pragma once

#include <cstdint>

#include "memory.hpp"

namespace Rtx
{
    /// **The blocks one owner that moves its content out of blocks gives back soon**, said to the allocator for as long
    /// as the owner stands: a block being emptied, or one it empties the next time a block is asked for
    /// (`MemoryAllocator::asksForBlock`). What a take refused meanwhile waits on rather than going without
    /// (`whenRoomless`): the refusal is what asks for the block.
    class BlocksComing
    {
    public:
        explicit BlocksComing(MemoryAllocator& memory)
            : mMemory(memory)
        {
        }

        ~BlocksComing() { set(0); }

        BlocksComing(const BlocksComing&) = delete;
        BlocksComing& operator=(const BlocksComing&) = delete;

        /// Gives back `blocks` soon from now on, in place of what it said before.
        void set(std::uint32_t blocks) noexcept
        {
            mMemory.noteBlocksComing(mBlocks, blocks);
            mBlocks = blocks;
        }

    private:
        MemoryAllocator& mMemory;
        std::uint32_t mBlocks = 0;
    };
}
