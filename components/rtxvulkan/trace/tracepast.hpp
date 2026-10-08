#pragma once

#include <cstdint>

namespace Rtx
{
    /// Whether a chain keeps what a trace leaves for the next one to read: the denoisers' histories
    /// and the air's.
    ///
    /// **A picture keeps none**: every picture is traced with its past lost, so no trace reads a
    /// history and one image of each pair holds everything a trace writes. The world's chain keeps
    /// both halves, the last trace's and this one's.
    enum class TracePast : std::uint8_t
    {
        Kept,
        Dropped,
    };
}
