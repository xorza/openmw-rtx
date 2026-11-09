#include "dlss.hpp"

namespace Platform::Dlss
{
    namespace
    {
        constexpr std::string_view sLibraryPrefix = "libnvidia-ngx-dlss.so.";

        constexpr Download sDownload{
            .mUrl
            = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Linux_x86_64/rel/"
              "libnvidia-ngx-dlss.so.310.9.16",
            .mFileName = "libnvidia-ngx-dlss.so.310.9.16",
            .mSha256 = "30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6",
            .mBytes = 59481944,
        };
        static_assert(sDownload.mUrl.find(sVersion) != std::string_view::npos, "a download of another release");
        static_assert(sDownload.mFileName.starts_with(sLibraryPrefix), "a download NGX would not find");
    }

    Download download()
    {
        return sDownload;
    }

    bool isFeatureLibrary(const std::filesystem::path& fileName)
    {
        return fileName.native().starts_with(sLibraryPrefix);
    }
}
