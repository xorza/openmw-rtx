#include "memory.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>

#include <components/misc/windows.hpp>

namespace Platform
{
    std::optional<std::uint64_t> availableMemory()
    {
        MEMORYSTATUSEX status{};
        status.dwLength = sizeof(status);
        if (!GlobalMemoryStatusEx(&status))
            return std::nullopt;
        return std::min<std::uint64_t>(status.ullAvailPhys, status.ullAvailPageFile);
    }
}
