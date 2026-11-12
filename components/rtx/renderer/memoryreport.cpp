#include "memoryreport.hpp"

#include <format>

namespace Rtx
{
    std::string describeMemory(const MemoryReport& report)
    {
        std::string out;
        for (std::uint32_t heap = 0; heap < report.mHeapCount; ++heap)
        {
            const HeapUse& use = report.mHeaps[heap];

            // The system's heap is named, because on a card with video memory of its own it can be the
            // largest, and read as video memory it is a card with more room than it has. The index
            // takes two places, which sixteen heaps need.
            out += std::format(
                "  heap {:>2}  {}  {:8.1f} MiB   ", heap, use.mDeviceLocal ? "device" : "system", mebibytes(use.mSize));
            out += std::format("reserved {:7.1f}   live {:7.1f}", mebibytes(use.mReserved), mebibytes(use.mLive));

            // Nought from a driver that would not say, which is not the same answer as a budget of
            // none — so the columns are left off rather than printed as zeroes.
            if (use.mBudget > 0)
                out += std::format("   budget {:7.1f}   held {:7.1f}", mebibytes(use.mBudget), mebibytes(use.mHeld));

            out += std::format("   {} blocks\n", use.mBlocks);
        }

        // What the video heap's `live` is made of, and why content stops where it does: the room
        // kept for the frame's largest targets, and what each kind of content may still take.
        const MemoryUses& uses = report.mUses;
        if (uses.mFrame + uses.mFrameReserve + uses.mEssential + uses.mStructures + uses.mTextures > 0)
            out += std::format(
                "  uses  frame {:.1f} of {:.1f} kept   essential {:.1f}   structures {:.1f}, room {:.1f}   "
                "textures {:.1f}, room {:.1f}\n",
                mebibytes(uses.mFrame), mebibytes(uses.mFrameReserve), mebibytes(uses.mEssential),
                mebibytes(uses.mStructures), mebibytes(uses.mStructureRoom), mebibytes(uses.mTextures),
                mebibytes(uses.mTextureRoom));

        return out;
    }
}
