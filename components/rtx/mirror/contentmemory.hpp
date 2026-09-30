#pragma once

#include <cstdint>

#include <components/rtx/mirror/cells/readermemory.hpp>

namespace Rtx
{
    /// What the content holds on the host beyond what stands in the scene, for a report taken once
    /// a place: how far the mesh runs reach against what they hold, since a freed mesh leaves a
    /// hole nothing moves to close, and what the cell reader keeps of the models it read.
    struct ContentMemory
    {
        std::uint32_t mVertexEnd = 0;
        std::uint32_t mVerticesUsed = 0;
        std::uint32_t mIndexEnd = 0;
        std::uint32_t mIndicesUsed = 0;
        ReaderMemory mReader;
    };
}
