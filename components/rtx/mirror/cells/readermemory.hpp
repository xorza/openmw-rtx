#pragma once

#include <cstdint>

namespace Rtx
{
    /// What a reader keeps of the models it read: the room their buffers grew to, lent to the
    /// frame, waiting as spares, and filed without a holder. A spare keeps the largest room any
    /// model it held grew to, so the spare figure is what a long session pays for the reader's
    /// never allocating.
    struct ReaderMemory
    {
        std::uint32_t mLentModels = 0;
        std::uint32_t mSpareModels = 0;

        /// Models filed and lent to no cell — refused, or with nothing to stand — which the reader
        /// keeps so that none of them is read twice.
        std::uint32_t mFiledModels = 0;

        std::uint64_t mLentBytes = 0;
        std::uint64_t mSpareBytes = 0;
        std::uint64_t mFiledBytes = 0;
    };
}
