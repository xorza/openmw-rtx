#include "spirvfile.hpp"

#include <cstddef>
#include <fstream>
#include <ios>
#include <string>

#include <spirv/unified1/spirv.hpp>

#include <components/rtx/common/error.hpp>

namespace Rtx
{
    std::string spelledPath(const std::filesystem::path& path)
    {
        const std::u8string spelled = path.u8string();
        return std::string(spelled.begin(), spelled.end());
    }

    std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw InputError("cannot open " + spelledPath(path));

        const std::streamsize size = stream.tellg();
        if (size <= 0 || size % 4 != 0)
            throw InputError(spelledPath(path) + " is " + std::to_string(size)
                + " bytes, which is not a whole number of SPIR-V words");

        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(words.data()), size);
        if (!stream)
            throw InputError("cannot read " + spelledPath(path));

        if (words.front() != spv::MagicNumber)
            throw InputError(spelledPath(path) + " does not begin with the SPIR-V magic number");

        return words;
    }
}
