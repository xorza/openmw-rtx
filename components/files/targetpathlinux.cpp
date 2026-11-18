#include "targetpath.hpp"

#include <array>
#include <utility>

#include <components/platform/userhome.hpp>

#include "wineutils.hpp"

namespace
{
    /// The platform's, which reads the password database reentrantly where the environment names no home.
    std::filesystem::path getUserHome()
    {
        return Platform::userHome().value_or(std::filesystem::path());
    }

    std::filesystem::path getEnv(const std::string& envVariable, const std::filesystem::path& fallback)
    {
        const char* result = getenv(envVariable.c_str());
        if (!result)
            return fallback;
        std::filesystem::path dir(result);
        if (dir.empty())
            return fallback;
        else
            return dir;
    }
}

/**
 * \namespace Files
 */
namespace Files
{

    std::string_view applicationName()
    {
        return "openmw";
    }

    TargetPath::TargetPath(std::string applicationName)
        : mName(std::move(applicationName))
    {
    }

    std::filesystem::path TargetPath::getUserConfigPath() const
    {
        return getEnv("XDG_CONFIG_HOME", getUserHome() / ".config") / mName;
    }

    std::filesystem::path TargetPath::getUserDataPath() const
    {
        return getEnv("XDG_DATA_HOME", getUserHome() / ".local/share") / mName;
    }

    std::filesystem::path TargetPath::getUserLocalDataPath() const
    {
        return getUserDataPath();
    }

    std::filesystem::path TargetPath::getCachePath() const
    {
        return getEnv("XDG_CACHE_HOME", getUserHome() / ".cache") / mName;
    }

    std::filesystem::path TargetPath::getGlobalConfigPath() const
    {
        return GLOBAL_CONFIG_PATH;
    }

    std::filesystem::path TargetPath::getLocalPath() const
    {
        auto localPath = std::filesystem::current_path() / "";

        static const std::filesystem::path statusPaths[]
            = { "/proc/self/exe", "/proc/self/file", "/proc/curproc/exe", "/proc/curproc/file" };

        for (const auto& path : statusPaths)
        {
            std::error_code ec;
            const auto binPath = read_symlink(path, ec);
            if (!ec)
            {
                localPath = binPath.parent_path() / "";
                break;
            }
        }

        return localPath;
    }

    std::filesystem::path TargetPath::getGlobalDataPath() const
    {
        return GLOBAL_DATA_PATH;
    }

    std::vector<std::filesystem::path> TargetPath::getInstallPaths() const
    {
        std::filesystem::path homePath = getUserHome();
        if (homePath.empty())
            return {};

        std::vector<std::filesystem::path> paths(Wine::getInstallPaths(homePath));
        std::array steamPaths{
            // Default (~/.steam/steam can be a symlink or a real directory)
            homePath / ".steam/steam/steamapps/common/Morrowind",
            // Snap
            homePath / "snap/steam/common/.local/share/Steam/steamapps/common/Morrowind",
            // Flatpak
            homePath / ".var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/common/Morrowind",
        };
        for (const std::filesystem::path& steam : steamPaths)
        {
            if (std::filesystem::is_directory(steam))
                paths.emplace_back(steam);
        }
        std::ranges::sort(paths);
        const auto [first, last] = std::ranges::unique(paths);
        paths.erase(first, last);
        return paths;
    }

} /* namespace Files */
