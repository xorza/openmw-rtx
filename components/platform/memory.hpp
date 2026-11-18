#pragma once

#include <cstdint>
#include <optional>

/// What the system has of memory for this process to take, which each system states by its own call:
/// one header over `memorylinux.cpp` and `memorywin32.cpp`.
namespace Platform
{
    /// The bytes this process may still allocate before the system pages them out or refuses them: on
    /// Linux the kernel's estimate (`MemAvailable`) less what of the process is in swap
    /// (`LinuxText::availableToProcess`), and where a cgroup v2 above the process caps its memory, the least room any
    /// such cap leaves (`memory.max` less `memory.current`); on Windows the least of the physical memory available
    /// and the commit the system may still charge, whose figure of what it paged out counts untouched memory with it.
    /// Nothing where the system does not say.
    std::optional<std::uint64_t> availableMemory();
}
