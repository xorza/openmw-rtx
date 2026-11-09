#include "dlss.hpp"

#include <components/platform/process.hpp>
#include <components/platform/userhome.hpp>

// The shell's headers use `far`/`near` in their RPC annotations, so they come before the tree's
// header takes those names away.
#include <Windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace Platform::Dlss
{
    namespace
    {
        std::filesystem::path executableDirectory()
        {
            if (const std::optional<std::filesystem::path> executable = Process::executable(); executable.has_value())
                return executable->parent_path();
            return {};
        }

        std::filesystem::path documentsDirectory()
        {
            PWSTR documents = nullptr;
            std::filesystem::path path;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents)) && documents != nullptr)
                path = documents;
            CoTaskMemFree(documents);

            if (path.empty())
                if (const std::optional<std::filesystem::path> home = userHome(); home.has_value())
                    path = *home / "Documents";
            return path;
        }
    }

    std::optional<Download> download()
    {
        return Download{
            .mUrl = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Windows_x86_64/rel/nvngx_dlss.dll",
            .mFileName = "nvngx_dlss.dll",
            .mSha256 = "d00b5b0ab29c3ca101b70c3a5ecb3a01b7ef33766a3383a28a18623083e6123e",
        };
    }

    std::filesystem::path runtimeDirectory()
    {
        const std::filesystem::path documents = documentsDirectory();
        if (documents.empty())
            return {};
        return documents / "My Games" / "openmw" / "dlss";
    }

    std::filesystem::path featureLibrary()
    {
        const std::optional<Download> feature = Platform::Dlss::download();
        if (!feature.has_value())
            return {};

        const std::filesystem::path directories[] = { runtimeDirectory(), executableDirectory() };
        for (const std::filesystem::path& directory : directories)
        {
            if (directory.empty())
                continue;

            const std::filesystem::path candidate = directory / feature->mFileName;
            if (std::filesystem::is_regular_file(candidate))
                return candidate;
        }

        return {};
    }

    bool installed()
    {
        return !featureLibrary().empty();
    }
}
