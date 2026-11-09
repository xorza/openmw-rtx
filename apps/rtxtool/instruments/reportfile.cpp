#include "reportfile.hpp"

#include <components/files/conversion.hpp>
#include <components/platform/wholefile.hpp>

namespace RtxTool
{
    Result<void, std::string> writeReport(const std::filesystem::path& path, const std::string_view bytes)
    {
        const Result<void, Platform::IoError> written = Platform::writeWhole(path, bytes, Platform::Durability::Whole);
        if (!written.isOk())
            return Err{ "could not write " + Files::pathToUnicodeString(path) + ": "
                + written.error().mCode.message() };
        return {};
    }
}
