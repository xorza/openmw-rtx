#include "dlss.hpp"

#include <Windows.h>

namespace Platform::Dlss
{
    namespace
    {
        constexpr Download sDownload{
            .mUrl = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Windows_x86_64/rel/nvngx_dlss.dll",
            .mFileName = "nvngx_dlss.dll",
            .mSha256 = "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e",
            .mBytes = 58956912,
        };
        static_assert(sDownload.mUrl.find(sVersion) != std::string_view::npos, "a download of another release");

        /// The download's name, as the system spells a file name.
        constexpr const wchar_t* sLibraryName = L"nvngx_dlss.dll";
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
