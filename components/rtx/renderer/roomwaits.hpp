#pragma once

#include <cstdint>

#include "framesinflight.hpp"

namespace Rtx
{
    /// **How many placements content the device had no room for waits before it is refused** (`docs/architecture.md`,
    /// "One residency policy for the content"): the room comes back once the frames in flight are done past the
    /// hand-over that gives back what rested for it, the one after the refusal, so three placements on the two frames
    /// in flight; twice that leaves room for an arrival in between. Past it the room is not coming, whatever rests: a
    /// take the free bytes would cover that the device's blocks do not. One bound for a structure, which waits on the
    /// device, and a texture, which waits in the uploader.
    inline constexpr std::uint32_t sMostRoomWaits = 2 * (sFramesInFlight + 1);
}
