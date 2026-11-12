#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include <components/rtx/common/mebibytes.hpp>

namespace Rtx
{
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

        /// Whether the heap is video memory at all, as the device states it: on a card with video
        /// memory of its own, the flag alone tells it from the system's, which can be the larger.
        bool mDeviceLocal = false;
    };

    /// What the renderer holds of its video memory by what each part is for, in the order the room
    /// is given in, and about what each kind of content may still take before it is refused. All
    /// nought from a renderer that does not say.
    struct MemoryUses
    {
        /// The frame's targets, an upscaler's own memory among them where its runtime allocates it
        /// (DLSS's feature), and the most they take at any mode the output allows, which content
        /// leaves room for.
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
        /// Fixed, so that a report can be copied into a bench record without an allocation: the
        /// most heaps a device may state, which the backend holds this to.
        static constexpr std::size_t sMaxHeaps = 16;

        std::array<HeapUse, sMaxHeaps> mHeaps{};
        std::uint32_t mHeapCount = 0;

        /// The video heap's memory by use.
        MemoryUses mUses{};
    };

    /// The report as the harness prints it, one line a heap and the uses' line under them where
    /// the renderer says, indented to sit under the place it belongs to.
    std::string describeMemory(const MemoryReport& report);
}
