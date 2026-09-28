#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace Rtx
{
    /// The words of the SPIR-V module at `path`, checked for being whole words and for beginning
    /// with the magic number: the one reader, for the modules the renderer loads and for the build's
    /// pin tool alike.
    ///
    /// @throws InputError naming `path` where it cannot be read or is not a module.
    std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path);
}
