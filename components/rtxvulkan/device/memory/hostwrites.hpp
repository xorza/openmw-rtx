#pragma once

#include <components/rtx/renderer/hostwrites.hpp>

namespace Rtx
{
    /// How this backend's host writes reach the device, on every device and decided here alone:
    /// another transport is another build, which is what an A/B of the two and a comparison of
    /// their pictures take. Both compile in every build (`docs/rtx/architecture.md`, "Host writes
    /// travel one way").
    inline constexpr HostWrites sHostWrites = HostWrites::Staged;
}
