#pragma once

#include <filesystem>
#include <optional>

/// The folder the system keeps as this user's own, which each system names its own way: one header
/// over `userhomeposix.cpp` and `userhomewin32.cpp`.
namespace Platform
{
    /// The user's home folder, or nothing where the system names none: `HOME`, or the password
    /// database's entry where it is unset, on a POSIX system, and the profile folder on Windows.
    std::optional<std::filesystem::path> userHome();
}
