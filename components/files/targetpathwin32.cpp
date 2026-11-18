#include "targetpath.hpp"

#include <algorithm>
#include <utility>

#define FAR
#define NEAR

#include <shlobj.h>
#include <shlwapi.h>
#include <winreg.h>

#undef NEAR
#undef FAR

#include <components/debug/debuglog.hpp>

/**
 * \namespace Files
 */
namespace Files
{
    namespace
    {
        struct RegistryKey
        {
            HKEY mKey = nullptr;

            RegistryKey() = default;
            RegistryKey(const RegistryKey&) = delete;
            RegistryKey& operator=(const RegistryKey&) = delete;

            ~RegistryKey()
            {
                if (mKey)
                    RegCloseKey(mKey);
            }
        };

        std::filesystem::path getRegistryPath(LPCWSTR subKey, LPCWSTR valueName, bool use32)
        {
            RegistryKey key;
            REGSAM flags = KEY_READ;
            if (use32)
                flags |= KEY_WOW64_32KEY;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey, 0, flags, &key.mKey) == ERROR_SUCCESS)
            {
                // Key existed, let's try to read the install dir: into the string's characters, which its size
                // counts and its capacity alone does not.
                std::wstring buffer(MAX_PATH, L'\0');
                auto len = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));

                auto result = RegQueryValueExW(
                    key.mKey, valueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &len);
                if (result == ERROR_MORE_DATA)
                {
                    buffer.resize(len / sizeof(wchar_t) + 1);
                    len = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
                    result = RegQueryValueExW(
                        key.mKey, valueName, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &len);
                }
                if (result == ERROR_SUCCESS)
                {
                    // This should always be true. Note that we don't need to care above because of the trailing \0
                    if (len % sizeof(wchar_t) == 0)
                    {
                        std::wstring_view view(buffer.data(), len / sizeof(wchar_t));
                        // Strip trailing \0 because the path constructor won't do it for us
                        const auto pos = view.find(L'\0');
                        if (pos != std::wstring_view::npos)
                            view = view.substr(0, pos);
                        return std::filesystem::path(view);
                    }
                }
            }
            return {};
        }
    }

    std::string_view applicationName()
    {
        return "OpenMW";
    }

    TargetPath::TargetPath(std::string applicationName)
        : mName(std::move(applicationName))
    {
    }

    std::filesystem::path TargetPath::getUserConfigPath() const
    {
        std::filesystem::path userPath = std::filesystem::current_path();

        PWSTR cString = nullptr;
        HRESULT result = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &cString);
        if (SUCCEEDED(result))
            userPath = std::filesystem::path(cString);
        else
            Log(Debug::Error) << "Error " << result << " when getting Documents path";

        CoTaskMemFree(cString);

        return userPath / "My Games" / mName;
    }

    std::filesystem::path TargetPath::getUserDataPath() const
    {
        // Have some chaos, windows people!
        return getUserConfigPath();
    }

    std::filesystem::path TargetPath::getGlobalConfigPath() const
    {
        // The concept of a global config path is absurd on Windows.
        // Always use local config instead.
        return {};
    }

    std::filesystem::path TargetPath::getLocalPath() const
    {
        std::filesystem::path localPath = std::filesystem::current_path() / "";

        std::wstring executablePath;
        DWORD copied = 0;
        do
        {
            executablePath.resize(executablePath.size() + MAX_PATH);
            copied = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
        } while (GetLastError() == ERROR_INSUFFICIENT_BUFFER);

        if (copied > 0)
        {
            localPath = std::filesystem::path(executablePath).parent_path() / "";
        }

        // lookup exe path
        return localPath;
    }

    std::filesystem::path TargetPath::getGlobalDataPath() const
    {
        return getGlobalConfigPath();
    }

    std::filesystem::path TargetPath::getCachePath() const
    {
        return getUserConfigPath() / "cache";
    }

    std::filesystem::path TargetPath::getUserLocalDataPath() const
    {
        // `%LOCALAPPDATA%` and not Documents, where the rest of the user's files are: Controlled
        // Folder Access guards Documents and OneDrive may sync it, neither what a downloaded
        // library wants.
        PWSTR cString = nullptr;
        std::filesystem::path localPath;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &cString)) && cString != nullptr)
            localPath = std::filesystem::path(cString) / mName;
        CoTaskMemFree(cString);
        return localPath;
    }

    std::vector<std::filesystem::path> TargetPath::getInstallPaths() const
    {
        std::vector<std::filesystem::path> paths;
        {
            std::filesystem::path disk
                = getRegistryPath(L"SOFTWARE\\Bethesda Softworks\\Morrowind", L"Installed Path", true);
            if (!disk.empty() && std::filesystem::is_directory(disk))
                paths.emplace_back(std::move(disk));
        }
        {
            std::filesystem::path gog = getRegistryPath(L"SOFTWARE\\GOG.com\\Games\\1435828767", L"path", true);
            if (!gog.empty() && std::filesystem::is_directory(gog))
                paths.emplace_back(std::move(gog));
        }
        {
            std::filesystem::path steam = getRegistryPath(
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 22320", L"InstallLocation", false);
            if (!steam.empty() && std::filesystem::is_directory(steam))
                paths.emplace_back(std::move(steam));
        }
        std::ranges::sort(paths);
        const auto [first, last] = std::ranges::unique(paths);
        paths.erase(first, last);
        return paths;
    }

} /* namespace Files */
