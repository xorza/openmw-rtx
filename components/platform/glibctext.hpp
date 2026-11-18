#pragma once

#include <optional>
#include <string_view>

#include "heap.hpp"

/// Text glibc writes about its allocator, read on every system: what asks glibc for it is the heap's own
/// (`heapposix.cpp`), and what it says is plain text a test on any machine can hand it.
namespace Platform::GlibcText
{
    /// What the heap holds and each arena apart, from the report `malloc_info` writes: an arena's taken is what it took
    /// from the system (`<system type="current">`), its free its fast bins' and other free chunks' bytes, the top
    /// chunk among them (`<total type="fast">`, `<total type="rest">`, each nought where glibc writes no line of it);
    /// the heap's taken is every arena's and the chunks mapped on their own (`<total type="mmap">`), which are in use
    /// until freed. Nothing where the report names no arena, an arena's taken or the mapped chunks' line is missing, or
    /// a size is not a number.
    std::optional<HeapHeld> heapHeld(std::string_view report);

    /// Whether `tunables`, `GLIBC_TUNABLES`' colon-separated `name=value` list, sets either of the allocator's settings
    /// `tuneHeap` pins (`glibc.malloc.arena_max`, `glibc.malloc.mmap_threshold`), which it then leaves as they are.
    /// Another of the allocator's, the huge pages a measured run restarts on (`restartOnHugePages`), leaves them to it.
    bool namesTheHeapsSettings(std::string_view tunables);
}
