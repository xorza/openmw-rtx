#pragma once

#include <compare>
#include <cstdint>

namespace Rtx
{
    /// **When a submit is done with what it read**: opaque and never falling, the backend's timeline in the core's
    /// words. A slot retired at an epoch is free once the device says it finished that epoch, which replaces every
    /// count of frames or hand-overs a wait was written as (`SlotLedger::retire`).
    struct DeviceEpoch
    {
        std::uint64_t mValue = 0;

        friend constexpr auto operator<=>(const DeviceEpoch&, const DeviceEpoch&) = default;
    };
}
