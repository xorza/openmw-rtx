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

    std::optional<HeapHeld> heapHeld()
    {
        // The C runtime's heap is the process's own.
        HEAP_SUMMARY summary{};
        summary.cb = sizeof(summary);
        if (HeapSummary(GetProcessHeap(), 0, &summary) == FALSE)
            return std::nullopt;
        return HeapHeld{ .mTaken = summary.cbCommitted, .mInUse = summary.cbAllocated };
    }
}
