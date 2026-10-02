#pragma once

#include <ctime>
#include <optional>

/// The calendar time where this process runs, which each system asks for by its own thread-safe
/// call: one header over `localtimeposix.cpp` and `localtimewin32.cpp`.
namespace Platform
{
    /// `seconds` since the epoch as the local calendar has them, or nothing where the system cannot
    /// say: a time past what its calendar holds.
    std::optional<std::tm> localTime(std::time_t seconds);
}
