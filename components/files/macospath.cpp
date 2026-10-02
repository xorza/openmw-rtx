#include "macospath.hpp"

#if defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__)

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <pwd.h>
#include <stdexcept>
#include <unistd.h>
#include <vector>

#include <components/platform/process.hpp>

#include "wineutils.hpp"

namespace
{
    std::filesystem::path getUserHome()
    {
        const char* dir = getenv("HOME");
        if (dir == nullptr)
        {
            struct passwd* pwd = getpwuid(getuid());
            if (pwd != nullptr)
            {
                dir = pwd->pw_dir;
            }
        }
        if (dir == nullptr)
            return std::filesystem::path();
        else
            return std::filesystem::path(dir);
    }
}

namespace Files
{

    MacOsPath::MacOsPath(const std::string& application_name)
        : mName(application_name)
    {
    }

    std::filesystem::path MacOsPath::getUserConfigPath() const
    {
        std::filesystem::path userPath(getUserHome());
        userPath /= "Library/Preferences/";

        return userPath / mName;
    }

    std::filesystem::path MacOsPath::getUserDataPath() const
    {
        std::filesystem::path userPath(getUserHome());
        userPath /= "Library/Application Support/";

        return userPath / mName;
    }

    std::filesystem::path MacOsPath::getGlobalConfigPath() const
    {
        std::filesystem::path globalPath("/Library/Preferences/");
        return globalPath / mName;
    }

    std::filesystem::path MacOsPath::getCachePath() const
    {
        std::filesystem::path userPath(getUserHome());
        userPath /= "Library/Caches";
        return userPath / mName;
    }

    std::filesystem::path MacOsPath::getLocalPath() const
    {
        const std::optional<std::filesystem::path> binaryPath = Platform::Process::executable();
        if (!binaryPath.has_value())
            throw std::runtime_error("Failed to get executable path");

        return binaryPath->parent_path().parent_path() / "Resources";
    }

    std::filesystem::path MacOsPath::getGlobalDataPath() const
    {
        std::filesystem::path globalDataPath("/Library/Application Support/");
        return globalDataPath / mName;
    }

    std::vector<std::filesystem::path> MacOsPath::getInstallPaths() const
    {
        std::filesystem::path homePath = getUserHome();
        if (homePath.empty())
            return {};

        std::vector<std::filesystem::path> paths(Wine::getInstallPaths(homePath));
        std::ranges::sort(paths);
        const auto [first, last] = std::ranges::unique(paths);
        paths.erase(first, last);
        return paths;
    }

} /* namespace Files */

#endif /* defined(macintosh) || defined(Macintosh) || defined(__APPLE__) || defined(__MACH__) */
