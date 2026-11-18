#pragma once

#include <cstdint>
#include <optional>
#include <vector>

/// The process's heap, which each system's allocator gives back to the system by its own call: one
/// header over `heapposix.cpp` and `heapwin32.cpp`.
namespace Platform
{
    /// Gives back to the system what the heap holds free, once a burst of work freed it: glibc keeps
    /// what its threads' arenas freed until a trim, which the loading screen's threads leave at most of
    /// a gigabyte. Nothing where the allocator returns it on its own. Costs a walk of the free lists, so
    /// it is called once a burst, never a frame.
    void trimHeap();

    /// **Caps glibc's arenas at four and maps every block of a mebibyte or more apart** (`mallopt(3)`), where
    /// `GLIBC_TUNABLES` sets neither, which stays the way to try another value: on the handheld's Repack
    /// tour the cap took 0.86 GiB off the process's most and 0.34 GiB off its swap, and on the desk's the heap's free
    /// memory from 396 to 97 MiB, the tours as fast and with fewer hitches
    /// (`docs/research/2026-11-17-handheld-allocator-tunables.md`). The threshold pinned turns glibc's own adjustment
    /// of it off, so a large buffer is given back whole, whichever thread frees it. Nothing where the allocator is not
    /// glibc's. Called once, by `init`, before any thread is made.
    void tuneHeap();

    /// One of the heap's arenas, which the allocator keeps apart, a thread taking one of its own: what it took from the
    /// system, and what of that is free.
    struct HeapArena
    {
        std::uint64_t mTaken = 0;
        std::uint64_t mFree = 0;
    };

    /// What the heap's allocator holds of the process's memory: what it took from the system, and what of that is
    /// in use, the rest free memory it keeps until a trim; and each arena apart, none where it keeps none apart or
    /// does not say.
    struct HeapHeld
    {
        std::uint64_t mTaken = 0;
        std::uint64_t mInUse = 0;
        std::vector<HeapArena> mArenas;
    };

    /// `HeapHeld` of this process, now: glibc's report (`malloc_info`), every glibc there is, and a sanitizer's
    /// allocator where one stands in its place, with no arenas. Walks every arena's free lists under their locks, so
    /// it is asked off the frame, once a second at most. Nothing where the allocator does not say, or says only by a
    /// walk of the whole heap under its lock, as Windows' process heap does (`HeapSummary`).
    std::optional<HeapHeld> heapHeld();

    /// Whether this system's allocator says what it holds (`heapHeld`): glibc's and a sanitizer's do, Windows' process
    /// heap does not.
    bool heapReports();
}
