#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

#include <components/platform/wholefile.hpp>
#include <components/rtx/common/error.hpp>

#include "spirvfile.hpp"
#include "spirvpin.hpp"

// `openmw-rtx-spirv-pin [--guard] <module>`: the module rewritten in place by `Rtx::pinFloatArithmetic`,
// the step the build runs on every shader before `spirv-val`, or with `--guard` by
// `Rtx::guardFloatArithmetic`, the step between `glslc` and the optimizer. In place, because each is a
// step over the one file of its stage.

namespace
{
    /// Written whole (`Platform::writeWhole`), so a build that stops halfway leaves the module as it
    /// was or as rewritten and never part of either.
    void writeWords(const std::filesystem::path& path, const std::vector<std::uint32_t>& words)
    {
        const Result<void, Platform::IoError> written
            = Platform::writeWhole(path, std::as_bytes(std::span(words)), Platform::Durability::Whole);
        if (!written.isOk())
            throw std::runtime_error("could not be written: " + written.error().mCode.message());
    }
}

int main(int argc, char* argv[])
{
    const bool guard = argc == 3 && std::string_view(argv[1]) == "--guard";
    if (argc != 2 && !guard)
    {
        std::cerr << "usage: openmw-rtx-spirv-pin [--guard] <module.spv>\n";
        return 2;
    }

    const std::filesystem::path path(argv[argc - 1]);
    std::vector<std::uint32_t> words;
    try
    {
        words = Rtx::readSpirv(path);
    }
    catch (const Rtx::InputError& error)
    {
        // Named already: the reader says which file it could not read.
        std::cerr << error.what() << '\n';
        return 1;
    }

    try
    {
        writeWords(path, guard ? Rtx::guardFloatArithmetic(words) : Rtx::pinFloatArithmetic(words));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << Rtx::spelledPath(path) << ": " << error.what() << '\n';
        return 1;
    }
}
