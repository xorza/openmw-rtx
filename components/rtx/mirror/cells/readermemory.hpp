#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a reader keeps of the models it read: the room their buffers grew to, lent to the
    /// frame and waiting as spares. A spare keeps the largest room any model it held grew to, so
    /// the spare figure is what a long session pays for the reader's never allocating.
    struct ReaderMemory
    {
        std::uint32_t mLentModels = 0;
        std::uint32_t mSpareModels = 0;
        std::uint64_t mLentBytes = 0;
        std::uint64_t mSpareBytes = 0;
    };
}
