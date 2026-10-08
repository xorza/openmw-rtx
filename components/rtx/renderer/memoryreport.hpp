#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Rtx
{
    /// `bytes` in mebibytes, which is the unit every report in this fork prints memory in.
    inline double megabytes(std::uint64_t bytes)
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    /// One of the device's memory heaps, and what this renderer has taken out of it.
    struct HeapUse
    {
        /// The heap itself, as the device states it.
        std::uint64_t mSize = 0;

        /// What the driver says this process may have of it — or the renderer's own budget where
        /// that is less (`RunProfile::mMemoryBudget`) — and what the driver says the process
        /// already holds: both nought where the driver will not say. A budget moves with what else
        /// is running, and is the figure a residency decision is made against.
        std::uint64_t mBudget = 0;
        std::uint64_t mHeld = 0;

        /// What the renderer's allocator asked this heap for, and what the resources inside those
        /// allocations occupy. The gap is the price of suballocating.
        std::uint64_t mReserved = 0;
        std::uint64_t mLive = 0;

        std::uint32_t mBlocks = 0;

        /// Whether the heap is video memory at all, as the device states it. A card without
        /// resizable BAR has three: its video memory, the system's, and a window of the first the
        /// host writes into — and only the flag tells the second from the first.
        bool mDeviceLocal = false;

        /// Whether the device reads this heap and the host writes into it directly: the one heap
        /// a card without resizable BAR keeps at a couple of hundred megabytes, and the whole of
        /// video memory on a card with it.
        bool mHostVisible = false;
    };

    /// What the renderer holds of its video memory by what each part is for, in the order the room
    /// is given in, and about what each kind of content may still take before it is refused. All
    /// nought from a renderer that does not say.
    struct MemoryUses
    {
        /// The frame's targets, and the most they take at any mode the output allows, which
        /// content leaves room for.
        std::uint64_t mFrame = 0;
        std::uint64_t mFrameReserve = 0;

        /// Everything else the frame cannot go without: its tables, the geometry every hit reads,
        /// and the staging behind uploads.
        std::uint64_t mEssential = 0;

        std::uint64_t mStructures = 0;
        std::uint64_t mStructureRoom = 0;
        std::uint64_t mTextures = 0;
        std::uint64_t mTextureRoom = 0;
    };

    /// Every heap of the device the renderer is running on.
    struct MemoryReport
    {
        /// Fixed, so that a report can be copied into a bench record without an allocation. No
        /// device this renderer targets states more heaps than this; a device that stated more
        /// would have the rest left out rather than counted wrongly.
        static constexpr std::size_t sMaxHeaps = 16;

        std::array<HeapUse, sMaxHeaps> mHeaps{};
        std::uint32_t mHeapCount = 0;

        /// What the renderer put in memory the host writes into and the device reads: the figure
        /// a card without resizable BAR runs out of, and one no heap line can state on a box whose
        /// whole 16 GiB is host-visible. A property of the memory type asked for, not of the heap.
        std::uint64_t mHostWrittenReserved = 0;
        std::uint64_t mHostWrittenLive = 0;

        /// The video heap's memory by use.
        MemoryUses mUses{};
    };

    /// The report as the harness prints it, one line a heap and the uses' line under them where
    /// the renderer says, indented to sit under the place it belongs to.
    std::string describeMemory(const MemoryReport& report);
}
