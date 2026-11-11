#include "dlss.hpp"

namespace DlssLibrary
{
    namespace
    {
        /// Ray Reconstruction's library, which NGX names for the feature as Super Resolution's is
        /// `libnvidia-ngx-dlss.so.` and frame generation's `libnvidia-ngx-dlssg.so.`.
        constexpr std::string_view sLibraryPrefix = "libnvidia-ngx-dlssd.so.";

        constexpr Download sDownload{
            .mUrl
            = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Linux_x86_64/rel/"
              "libnvidia-ngx-dlssd.so.310.9.16",
            .mFileName = "libnvidia-ngx-dlssd.so.310.9.16",
            .mSha256 = "a5d59de18841f15779039dda567a15dd723ba737915f489271fdb5f109fbfb96",
            .mBytes = 48238232,
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
