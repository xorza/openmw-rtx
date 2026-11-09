#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

/// The DLSS feature library a player supplies: what the launcher downloads, and how a folder is
/// known to hold one. The driver itself supplies the NGX core; only the feature library is per
/// application.
namespace Platform::Dlss
{
    /// Whether this build has the DLSS backend at all, the one reading of the build flag.
    /// `OPENMW_RTX_DLSS=OFF` builds the FSR-only renderer, and the interfaces must not offer a choice
    /// that build cannot honour.
    inline constexpr bool sBuilt = OPENMW_RTX_DLSS;

    /// The NVIDIA release the download and its checksum are pinned to. It is the release the fork
    /// was tested with, not whatever is newest; each system's download names it in its address.
    inline constexpr std::string_view sVersion = "310.9.16";

    /// The licence NVIDIA attaches to the library the launcher downloads. The launcher links to it
    /// beside the button; it is not fetched, and there is no click-through, because the licence
    /// applies by use and the player downloads the file from NVIDIA directly.
    inline constexpr std::string_view sLicenseUrl = "https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt";
    static_assert(sLicenseUrl.find(sVersion) != std::string_view::npos, "the licence of another release");

    struct Download
    {
        std::string_view mUrl;
        std::string_view mFileName;
        std::string_view mSha256;

        /// The pinned file's size: the most a download reads before it is refused, so a server that
        /// answers with something else cannot fill the memory or the disk.
        std::uint64_t mBytes;
    };

    /// What this system downloads. The launcher offers it only on a machine with an NVIDIA
    /// adapter, and the game only uses it when the player asks for DLSS.
    Download download();

    /// The folders DLSS's files are found in, as the host's configuration names them
    /// (`Files::ConfigurationManager`): nothing here picks a folder of its own.
    struct Folders
    {
        /// Where the launcher downloads the feature library, in the user's local data
        /// (`foldersIn`): the folder NGX is handed to search, and the one it keeps its files in.
        std::filesystem::path mRuntime;

        /// The game's own folder, which NGX searches by default and a player may put a library in.
        std::filesystem::path mGame;
    };

    /// The folders for a host whose configuration names `userLocalData`
    /// (`Files::ConfigurationManager::getUserLocalDataPath`) and the game's own folder: the runtime
    /// folder is `dlss` in the first.
    Folders foldersIn(const std::filesystem::path& userLocalData, const std::filesystem::path& game);

    /// Whether `fileName` names a feature library NGX loads on this system: `nvngx_dlss.dll` on
    /// Windows, and on Linux `libnvidia-ngx-dlss.so.` and any version, as NGX searches for it.
    bool isFeatureLibrary(const std::filesystem::path& fileName);

    /// Whether either folder holds a feature library. A folder that is unnamed, empty or cannot be
    /// listed holds none.
    bool installedIn(const Folders& folders);
}
