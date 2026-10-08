#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace Platform
{
    /// Every entry of `folder`, listed whole before the caller acts on any of them, or nothing where
    /// the folder could not be opened or the listing broke off: a caller that removes what it lists
    /// then removes from a list and not from under an iterator, which may or may not visit what
    /// follows a removal. **Stepped by `increment` and its error code**, because a range-for over a
    /// `directory_iterator` takes the error code at the start alone, and its every step throws.
    std::optional<std::vector<std::filesystem::directory_entry>> listFolder(const std::filesystem::path& folder);
}
