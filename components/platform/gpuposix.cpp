#include "gpu.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace Platform::Gpu
{
    namespace
    {
        std::optional<std::uint32_t> vendorAt(const std::filesystem::path& path)
        {
            std::ifstream file(path);
            std::string text;
            if (!(file >> text))
                return std::nullopt;

            try
            {
                return static_cast<std::uint32_t>(std::stoul(text, nullptr, 0));
            }
            catch (...)
            {
                return std::nullopt;
            }
        }
    }

    std::vector<std::uint32_t> vendors()
    {
        std::vector<std::uint32_t> found;

        std::error_code error;
        for (const std::filesystem::directory_entry& entry :
            std::filesystem::directory_iterator("/sys/class/drm", error))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (!name.starts_with(L"card") || name.find(L'-') != std::wstring::npos)
                continue;

            if (const std::optional<std::uint32_t> vendor = vendorAt(entry.path() / "device" / "vendor");
                vendor.has_value())
                found.push_back(*vendor);
        }

        std::sort(found.begin(), found.end());
        found.erase(std::unique(found.begin(), found.end()), found.end());
        return found;
    }

    bool has(const std::uint32_t vendor)
    {
        const std::vector<std::uint32_t> all = vendors();
        return std::find(all.begin(), all.end(), vendor) != all.end();
    }
}
