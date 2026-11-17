#include "heap.hpp"

#include <cstdint>
#include <optional>

#include <malloc.h>

#include <components/misc/windows.hpp>

namespace Platform
{
    void trimHeap()
    {
        _heapmin();
    }

    // The process heap keeps no arenas and no threshold to pin.
    void tuneHeap() {}

    std::optional<HeapHeld> heapHeld()
    {
        // The C runtime's heap is the process's own.
        HEAP_SUMMARY summary{};
        summary.cb = sizeof(summary);
        if (HeapSummary(GetProcessHeap(), 0, &summary) == FALSE)
            return std::nullopt;
        return HeapHeld{ .mTaken = summary.cbCommitted, .mInUse = summary.cbAllocated, .mArenas = {} };
    }
}
