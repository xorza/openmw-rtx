#include "dlss.hpp"

#include <string>

#include <components/platform/process.hpp>
#include <components/platform/userhome.hpp>

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
    }

    std::optional<Download> download()
    {
        return Download{
            .mUrl
            = "https://raw.githubusercontent.com/NVIDIA/DLSS/v310.9.16/lib/Linux_x86_64/rel/"
              "libnvidia-ngx-dlss.so.310.9.16",
            .mFileName = "libnvidia-ngx-dlss.so.310.9.16",
            .mSha256 = "30982c937afed85c96b8d313323de9a650a2eec6de1bb0f714b21750f4ce98f6",
        };
    }

    std::filesystem::path runtimeDirectory()
    {
        std::filesystem::path base;
        if (const std::optional<std::filesystem::path> data = Process::environmentPath("XDG_DATA_HOME");
            data.has_value() && !data->empty())
            base = *data;
        else if (const std::optional<std::filesystem::path> home = userHome(); home.has_value())
            base = *home / ".local" / "share";

        if (base.empty())
            return {};
        return base / "openmw" / "dlss";
    }

    std::filesystem::path featureLibrary()
    {
        const std::filesystem::path directories[] = { runtimeDirectory(), executableDirectory() };
        for (const std::filesystem::path& directory : directories)
        {
            if (directory.empty())
                continue;

            std::error_code error;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
            {
                const std::wstring name = entry.path().filename().wstring();
                if (name.starts_with(L"libnvidia-ngx-dlss.so."))
                    return entry.path();
            }
        }

        return {};
    }

    bool installed()
    {
        return !featureLibrary().empty();
    }
}
