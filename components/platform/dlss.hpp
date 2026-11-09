#pragma once

#include <filesystem>
#include <string_view>

/// The DLSS feature library a player supplies, and where the launcher puts one it downloads. The
/// driver itself supplies the NGX core; only the feature library is per application.
namespace Platform::Dlss
{
    /// Whether this build has the DLSS backend at all. `OPENMW_RTX_DLSS=OFF` builds the FSR-only
    /// renderer, and the interfaces must not offer a choice that build cannot honour.
    inline constexpr bool sBuilt = OPENMW_RTX_DLSS;

    /// The NVIDIA release the download and its checksum are pinned to. It is the release the fork
    /// was tested with, not whatever is newest.
    inline constexpr std::string_view sVersion = "310.9.16";

    /// The licence NVIDIA attaches to the library the launcher downloads. The launcher links to it
    /// beside the button; it is not fetched, and there is no click-through, because the licence
    /// applies by use and the player downloads the file from NVIDIA directly.
    inline constexpr std::string_view sLicenseUrl = "https://github.com/NVIDIA/DLSS/blob/v310.9.16/LICENSE.txt";

    struct Download
    {
        std::string_view mUrl;
        std::string_view mFileName;
        std::string_view mSha256;
    };

    /// What this system downloads. The launcher offers it only on a machine with an NVIDIA
    /// adapter, and the game only uses it when the player asks for DLSS.
    Download download();

    /// The user folder the launcher downloads into and the game searches besides its own folder:
    /// under `My Games/openmw` on Windows and `openmw` in the XDG data folder on Linux. Empty where
    /// the system names no such folder.
    std::filesystem::path runtimeDirectory();

    /// The writable folder NGX keeps its log in, the same for discovery and initialisation: the
    /// runtime folder, made if it does not stand, or the system's temporary folder where the system
    /// names no user folder. Never the working directory, which an AppImage or Program Files can
    /// make read-only.
    std::filesystem::path applicationDataDirectory();

    /// The feature library in either the runtime folder or the game's own folder, or empty where
    /// there is none. The game's own folder is where NGX looks by default.
    std::filesystem::path featureLibrary();

    /// Whether `featureLibrary` found one.
    bool installed();
}
