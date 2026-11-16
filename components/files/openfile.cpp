#include "openfile.hpp"

#include <fstream>

#include "conversion.hpp"

namespace Files
{
    std::unique_ptr<std::ifstream> openBinaryInputFileStream(const std::filesystem::path& path)
    {
        auto stream = std::make_unique<std::ifstream>(path, std::ios::binary);
        if (!stream->is_open())
            throw std::system_error(errno, std::generic_category(),
                "Failed to open '" + Files::pathToUnicodeString(path) + "' for reading");
        stream->exceptions(std::ios::badbit);
        return stream;
    }
}
