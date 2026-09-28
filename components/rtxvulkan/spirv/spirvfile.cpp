#include "spirvfile.hpp"

#include <fstream>
#include <ios>
#include <string>

#include <components/rtx/common/error.hpp>

namespace Rtx
{
    namespace
    {
        constexpr std::uint32_t sSpirvMagic = 0x07230203;

        /// The path as UTF-8, as `Files::pathToUnicodeString` spells it: this library links nothing
        /// of the engine's, because every shader waits for the tool built from it.
        std::string nameOf(const std::filesystem::path& path)
        {
            const std::u8string spelled = path.u8string();
            return std::string(spelled.begin(), spelled.end());
        }
    }

    std::vector<std::uint32_t> readSpirv(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream)
            throw InputError("cannot open " + nameOf(path));

        const std::streamsize size = stream.tellg();
        if (size <= 0 || size % 4 != 0)
            throw InputError(
                nameOf(path) + " is " + std::to_string(size) + " bytes, which is not a whole number of SPIR-V words");

        std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / 4);
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(words.data()), size);
        if (!stream)
            throw InputError("cannot read " + nameOf(path));

        if (words.front() != sSpirvMagic)
            throw InputError(nameOf(path) + " does not begin with the SPIR-V magic number");

        return words;
    }
}
