#include "dlss.hpp"

#include <Windows.h>

namespace DlssLibrary
{
    namespace
    {
        constexpr Download sDownload{
            .mUrl = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Windows_x86_64/rel/nvngx_dlssd.dll",
            .mFileName = "nvngx_dlssd.dll",
            .mSha256 = "b49435ae346f227feafe7bbe937d88277ea0ee7323975ec06488f377fdb46cee",
            .mBytes = 48343664,
        };
        static_assert(sDownload.mUrl.find(sVersion) != std::string_view::npos, "a download of another release");

        /// The download's name, as the system spells a file name: Ray Reconstruction's library, and not
        /// Super Resolution's `nvngx_dlss.dll` or frame generation's `nvngx_dlssg.dll`.
        constexpr const wchar_t* sLibraryName = L"nvngx_dlssd.dll";
    }

    Download download()
    {
        return sDownload;
    }

    bool isFeatureLibrary(const std::filesystem::path& fileName)
    {
        // A file name on Windows is the same file in any case, and NGX's load finds it so.
        return CompareStringOrdinal(fileName.c_str(), -1, sLibraryName, -1, TRUE) == CSTR_EQUAL;
    }
}
