#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Rtx
{
    /// The words of the SPIR-V module at `path`, checked for being whole words and for beginning
    /// with the magic number: the one reader, for the modules the renderer loads and for the build's
    /// pin tool alike.
    ///
    /// @throws InputError naming `path` where it cannot be read or is not a module.
    std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path);

    /// `path` as UTF-8, as `Files::pathToUnicodeString` spells it: this library links nothing of the
    /// engine's, because every shader waits for the tool built from it.
    std::string spelledPath(const std::filesystem::path& path);
}
