#pragma once

#include <filesystem>

namespace Platform::Dlss
{
    /// The folder the executable is in, where NGX looks first; empty where the system cannot name
    /// it. Shared by the platform library searches, not part of the public `dlss.hpp` interface.
    std::filesystem::path executableDirectory();
}
