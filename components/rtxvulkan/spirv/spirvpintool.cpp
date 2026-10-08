#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include <components/rtx/common/error.hpp>

#include "spirvfile.hpp"
#include "spirvpin.hpp"

// `openmw-rtx-spirv-pin <module>`: the module rewritten in place by `Rtx::pinFloatArithmetic`, which
// is the step the build runs on every shader between `glslc` and `spirv-val`. In place, because the
// file `glslc` wrote is the one its depfile names.

namespace
{
    /// Written beside the module and moved over it, so a build that stops halfway leaves the
    /// module `glslc` wrote or the pinned one and never part of either.
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
    if (argc != 2)
    {
        std::cerr << "usage: openmw-rtx-spirv-pin <module.spv>\n";
        return 2;
    }

    const std::filesystem::path path(argv[1]);
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
        writeWords(path, Rtx::pinFloatArithmetic(words));
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << Rtx::spelledPath(path) << ": " << error.what() << '\n';
        return 1;
    }
}
