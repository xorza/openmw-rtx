#include "heap.hpp"

#include <optional>

#include <malloc.h>

namespace Platform
{
    void trimHeap()
    {
        _heapmin();
    }

    // The process heap keeps no arenas and no threshold to pin.
    void tuneHeap() {}

    bool heapReports()
    {
        return false;
    }

    std::optional<HeapHeld> heapHeld()
    {
        // **Nothing: the process heap's one figure walks it under its lock.** `HeapSummary` held the Windows machine's
        // 2.5 GiB heap 1.2 s at each of the caches' reads, once a second, every allocating thread waiting on the lock
        // and the frame with them; the PBR tour's legs' p99 rose from 29 ms to 1.2 s. What the heaps hold of the
        // process is `Process::privateMemoryHeld`'s, once a run, at its end.
        return std::nullopt;
    }
}
