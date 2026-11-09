#pragma once

/// The process's heap, which each system's allocator gives back to the system by its own call: one
/// header over `heapposix.cpp` and `heapwin32.cpp`.
namespace Platform
{
    /// Gives back to the system what the heap holds free, once a burst of work freed it: glibc keeps
    /// what its threads' arenas freed until a trim, which the loading screen's threads leave at most of
    /// a gigabyte. Nothing where the allocator returns it on its own. Costs a walk of the free lists, so
    /// it is called once a burst, never a frame.
    void trimHeap();
}
