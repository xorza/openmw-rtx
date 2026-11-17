#pragma once

#include <cstdint>

namespace Rtx
{
    /// What `PreparedImages` holds that no slot was handed yet, and how many slots let go of their file while its entry
    /// was still being read or made (`PreparedImages::getHeld`). Apart from it, so a report names it without the
    /// threads and tables it is read from.
    struct PreparedHeld
    {
        std::uint32_t mEntries = 0;
        std::uint64_t mBytes = 0;
        std::uint64_t mLeftPending = 0;
    };
}
