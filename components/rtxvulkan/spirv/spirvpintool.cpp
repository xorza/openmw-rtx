#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <vector>

#include <components/rtx/common/error.hpp>

#include "spirvfile.hpp"
#include "spirvpin.hpp"

// `openmw-rtx-spirv-pin [--guard] <module>`: the module rewritten in place by `Rtx::pinFloatArithmetic`,
// the step the build runs on every shader before `spirv-val`, or with `--guard` by
// `Rtx::guardFloatArithmetic`, the step between `glslc` and the optimizer. In place, because each is a
// step over the one file of its stage.

namespace
{
    /// Written beside the module and moved over it, so a build that stops halfway leaves the
    /// module as it was or as rewritten and never part of either.
    void writeWords(const std::filesystem::path& path, const std::vector<std::uint32_t>& words)
    {
        std::filesystem::path written = path;
        written += ".pinning";
        {
            std::ofstream stream(written, std::ios::binary | std::ios::trunc);
            stream.write(reinterpret_cast<const char*>(words.data()),
                static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
            if (!stream)
                throw std::runtime_error("could not be written");
        }

        std::error_code failed;
        std::filesystem::rename(written, path, failed);
        if (failed)
            throw std::runtime_error("could not be replaced: " + failed.message());
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
