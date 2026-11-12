#pragma once

#include <cstdint>

namespace Rtx
{
    /// How many frames may be in flight over one scene at once. Two, because the host is one frame
    /// ahead of the device and no more: what frame N+1's walk writes cannot be what N reads, and a
    /// third would buy nothing. The backend keeps a copy of every table a frame writes for each,
    /// and a measured run holds the game to keeping this many.
    inline constexpr std::uint32_t sFramesInFlight = 2;
}
