#include "dlss.hpp"

#include <algorithm>
#include <optional>
#include <system_error>
#include <vector>

#include <components/platform/folder.hpp>

namespace Platform::Dlss
{
    Folders foldersIn(const std::filesystem::path& userLocalData, const std::filesystem::path& game)
    {
        return Folders{ .mRuntime = userLocalData.empty() ? std::filesystem::path() : userLocalData / "dlss",
            .mGame = game };
    }

    bool installedIn(const Folders& folders)
    {
        const auto holdsLibrary = [](const std::filesystem::path& directory) {
            if (directory.empty())
                return false;

            const std::optional<std::vector<std::filesystem::directory_entry>> entries = listFolder(directory);
            return entries.has_value()
                && std::ranges::any_of(*entries, [](const std::filesystem::directory_entry& entry) {
                       std::error_code error;
                       return entry.is_regular_file(error) && isFeatureLibrary(entry.path().filename());
                   });
        };
        return holdsLibrary(folders.mRuntime) || holdsLibrary(folders.mGame);
    }
}
