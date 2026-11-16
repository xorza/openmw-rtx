#pragma once

#include <cstdint>
#include <optional>

/// The process's heap, which each system's allocator gives back to the system by its own call: one
/// header over `heapposix.cpp` and `heapwin32.cpp`.
namespace Platform
{
    /// Gives back to the system what the heap holds free, once a burst of work freed it: glibc keeps
    /// what its threads' arenas freed until a trim, which the loading screen's threads leave at most of
    /// a gigabyte. Nothing where the allocator returns it on its own. Costs a walk of the free lists, so
    /// it is called once a burst, never a frame.
    void trimHeap();

    /// What the heap's allocator holds of the process's memory: what it took from the system, and what of that is
    /// in use; the rest is free memory it keeps until a trim.
    struct HeapHeld
    {
        std::uint64_t mTaken = 0;
        std::uint64_t mInUse = 0;
    };

    /// `HeapHeld` of this process, now: glibc's `mallinfo2`, Windows' process heap. Walks every arena's free
    /// lists under their locks, so it is asked once a stop, never a frame. Nothing where the allocator does not say.
    std::optional<HeapHeld> heapHeld();
}
