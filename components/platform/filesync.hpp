#pragma once

#include <filesystem>

namespace Platform
{
    /// Asks the system to put what was written to `path` on the disk before this returns: what a
    /// file renamed over another needs first, or a power loss after the rename can leave the new
    /// name on a body that was never written. False where the file could not be opened or the
    /// system refused, which leaves the data where the system's own flush puts it.
    bool syncFile(const std::filesystem::path& path);
}
