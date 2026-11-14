#pragma once

#include <cstdint>

namespace RtxTool
{
    /// One of the driver's own samples of a process on the card: which process, the driver's
    /// clock when it was taken, and whether the process had the SMs or the memory busy in it.
    struct CardSample
    {
        std::uint32_t mPid = 0;
        std::uint64_t mStamp = 0;
        bool mHeld = false;
    };
}
