#pragma once

#include <string>

#include <components/platform/heap.hpp>

namespace RtxTool
{
    /// One clause for a person: what `heap` took and what of it is free, and where the allocator keeps arenas apart,
    /// how many and the four that keep the most free, so a stop's line says whether the free memory is spread thin
    /// across many arenas or stranded in a few.
    std::string describeHeap(const Platform::HeapHeld& heap);
}
