#include "folder.hpp"

#include <system_error>

namespace Platform
{
    std::optional<std::vector<std::filesystem::directory_entry>> listFolder(const std::filesystem::path& folder)
    {
        std::vector<std::filesystem::directory_entry> entries;
        std::error_code error;
        for (std::filesystem::directory_iterator entry(folder, error), end; !error && entry != end;
             entry.increment(error))
            entries.push_back(*entry);
        if (error)
            return std::nullopt;
        return entries;
    }
}
