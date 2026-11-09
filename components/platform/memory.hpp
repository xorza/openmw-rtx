#pragma once

#include <cstdint>
#include <optional>

/// What the system has of memory for this process to take, which each system states by its own call:
/// one header over `memoryposix.cpp` and `memorywin32.cpp`.
namespace Platform
{
    /// The bytes this process may still allocate before the system pages them out or refuses them: on
    /// Linux the kernel's estimate (`MemAvailable`), and where a cgroup v2 above the process caps its
    /// memory, the least room any such cap leaves (`memory.max` less `memory.current`); on Windows the
    /// least of the physical memory available and the commit the system may still charge. Nothing
    /// where the system does not say, as macOS and the BSDs do not.
    std::optional<std::uint64_t> availableMemory();
}
