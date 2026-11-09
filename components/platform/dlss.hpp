#pragma once

#include <filesystem>
#include <optional>
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

    inline constexpr std::string_view sLicenseUrl
        = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/LICENSE.txt";
    inline constexpr std::string_view sLicenseSha256
        = "d4216e39ebef5f9b50a6712ebb37beeb5379862a67733a9999c651f21592aaf0";

    struct Download
    {
        std::string_view mUrl;
        std::string_view mFileName;
        std::string_view mSha256;
    };

    /// What this system downloads, or nothing where DLSS cannot run here.
    std::optional<Download> download();

    /// The user folder the launcher downloads into and the game searches besides its own folder:
    /// under `My Games/openmw` on Windows and `openmw` in the XDG data folder on Linux. Empty where
    /// the system names no such folder.
    std::filesystem::path runtimeDirectory();

    /// The feature library in either the runtime folder or the game's own folder, or empty where
    /// there is none. The game's own folder is where NGX looks by default.
    std::filesystem::path featureLibrary();

    /// Whether `featureLibrary` found one.
    bool installed();
}
