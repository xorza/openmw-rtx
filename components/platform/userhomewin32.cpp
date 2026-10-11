#include "userhome.hpp"

#include <components/misc/windows.hpp>

// After `<windows.h>`, whose declarations the shell's headers assume.
#include <knownfolders.h>
#include <shlobj.h>

namespace Platform
{
    std::optional<std::filesystem::path> userHome()
    {
        PWSTR profile = nullptr;
        const HRESULT result = SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile);
        std::optional<std::filesystem::path> home;
        if (SUCCEEDED(result) && profile != nullptr && *profile != L'\0')
            home = std::filesystem::path(profile);

        // Freed whether or not the call succeeded, as `SHGetKnownFolderPath` asks.
        CoTaskMemFree(profile);
        return home;
    }
}
