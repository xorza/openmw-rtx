#include "spirvfile.hpp"

#include <cstddef>
#include <cstring>
#include <string>

#include <spirv/unified1/spirv.hpp>

#include <components/platform/wholefile.hpp>
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
        std::vector<std::byte> bytes;
        if (const Result<void, Platform::IoError> read = Platform::readWhole(path, bytes); !read.isOk())
            throw InputError("cannot read " + spelledPath(path) + ": " + read.error().mCode.message());

        if (bytes.empty() || bytes.size() % 4 != 0)
            throw InputError(spelledPath(path) + " is " + std::to_string(bytes.size())
                + " bytes, which is not a whole number of SPIR-V words");

        std::vector<std::uint32_t> words(bytes.size() / 4);
        std::memcpy(words.data(), bytes.data(), bytes.size());

        if (words.front() != spv::MagicNumber)
            throw InputError(spelledPath(path) + " does not begin with the SPIR-V magic number");

        return words;
    }
}
