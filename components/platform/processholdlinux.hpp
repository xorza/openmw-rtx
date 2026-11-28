#pragma once

#include <cstdint>

#include "processhold.hpp"

/// What `ProcessHold` holds a process by on Linux alone, and built there alone.
namespace Platform
{
    /// **`id` held by the time it started, as `ProcessHold` holds a process where Linux opens no pidfd**: below 5.3.
    /// Nothing where its `stat` cannot be read or states it ended. Asked by a caller only to hold a process so on a
    /// kernel that has pidfds, as a test of the way does.
    ProcessHold holdByStartTime(std::uint32_t id);
}
