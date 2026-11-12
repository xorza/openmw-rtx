#pragma once

#include <cstdint>

namespace Rtx
{
    /// `bytes` in mebibytes, the unit every report in this fork prints memory in: a fraction, so a
    /// figure under one prints as one and not as nought.
    inline double mebibytes(const std::uint64_t bytes)
    {
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }
}
