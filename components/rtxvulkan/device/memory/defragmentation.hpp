#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan_core.h>

struct VmaAllocation_T;
struct VmaDefragmentationContext_T;
struct VmaDefragmentationMove;
struct VmaPool_T;

namespace Rtx
{
    class MemoryAllocator;

    /// One range a defragmentation pass moves: the range, as the `DeviceMemory` that holds it names it
    /// (`DeviceMemory::isMovedBy`), and where the pass places it, at the same size and alignment.
    struct MemoryMove
    {
        const VmaAllocation_T* mRange = nullptr;
        VkDeviceMemory mTo = VK_NULL_HANDLE;
        VkDeviceSize mToOffset = 0;

        /// Where the move stands among the pass's own, which `Defragmentation::ignore` names it by.
        std::uint32_t mAt = 0;
    };

    /// What one defragmentation moved and gave back, over every pass.
    struct DefragmentationMoved
    {
        VkDeviceSize mBytes = 0;
        std::uint32_t mRanges = 0;
        VkDeviceSize mBytesFreed = 0;
        std::uint32_t mBlocksFreed = 0;
    };

    /// **The ranges one content pool strands in pieces, moved together**, a pass at a time: VMA's incremental
    /// defragmentation (`docs/architecture.md`, "What the texture pool strands in pieces is moved, not classed"). A
    /// pass names the ranges it moves (`beginPass`). Their owners make each resource again at its new place, its range
    /// going with it (`MemoryAllocator::moved`), and copy it there; the pass ends once those copies and every read of
    /// the old resources have run (`endPass`), which gives back the blocks it emptied. A move no owner makes is
    /// ignored, and a range a pass names that its owner lets go of meanwhile is given back once the pass ends.
    ///
    /// One at a time, on the device's thread: `MemoryAllocator::defragment` makes it.
    class Defragmentation
    {
    public:
        ~Defragmentation();

        Defragmentation(const Defragmentation&) = delete;
        Defragmentation& operator=(const Defragmentation&) = delete;
        Defragmentation(Defragmentation&& other) noexcept;
        Defragmentation& operator=(Defragmentation&& other) noexcept;

        /// Opens the next pass, its moves into `moves`, cleared first and sorted by range. False where nothing is
        /// left to move, which opens none and ends the defragmentation.
        bool beginPass(std::vector<MemoryMove>& moves);

        /// The open pass's move `at` (`MemoryMove::mAt`) is not made: its range stays where it is.
        void ignore(std::uint32_t at);

        /// Says the open pass's copies ride the submit that signals `value`: the pass may end once it has run.
        void copiedBy(std::uint64_t value) { mCopiedBy = value; }

        /// Whether the open pass may end: its copies, where it has any, have run.
        bool isCopied() const;

        /// Ends the open pass, once `isCopied`: every range its moves name stands where the move placed it, the
        /// ranges its owners let go of meanwhile are given back, and the blocks it emptied go back to the device.
        /// False where the defragmentation is done.
        bool endPass();

        /// Whether a pass is open.
        bool isPassOpen() const { return mMoves != nullptr; }

        /// What the passes moved so far, and the blocks given back once the defragmentation ended.
        const DefragmentationMoved& getMoved() const { return mMoved; }

    private:
        friend class MemoryAllocator;

        Defragmentation(
            MemoryAllocator& owner, VmaPool_T* pool, VkDeviceSize bytesPerPass, std::uint32_t rangesPerPass);

        /// Ends the defragmentation where one stands, waiting out the open pass's copies first: what a scene taken
        /// apart with a pass open asks.
        void end() noexcept;

        /// Ends the library's context, with no pass open, reading what it gave back.
        void finish() noexcept;

        MemoryAllocator* mOwner = nullptr;
        VmaDefragmentationContext_T* mContext = nullptr;

        /// The open pass's moves, the library's own, which it reads again as the pass ends; null where none is open.
        VmaDefragmentationMove* mMoves = nullptr;
        std::uint32_t mMoveCount = 0;

        /// The submit the open pass's copies ride, nought where it has none.
        std::uint64_t mCopiedBy = 0;

        DefragmentationMoved mMoved;
    };
}
