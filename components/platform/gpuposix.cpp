#include "gpu.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <components/platform/folder.hpp>
#include <components/platform/linuxtext.hpp>
#include <components/platform/wholefile.hpp>

namespace Platform::Gpu
{
    namespace
    {
        /// One page: what sysfs states as the size of every attribute it serves, the seven bytes of
        /// `0x10de` and a line break among them, and the most one can hold.
        constexpr std::size_t sIdFileBytesMax = 4096;
    }

    std::vector<std::uint32_t> vendors()
    {
        std::vector<std::uint32_t> found;

        const std::optional<std::vector<std::filesystem::directory_entry>> cards = listFolder("/sys/class/drm");
        if (!cards.has_value())
            return found;

        std::string text;
        for (const std::filesystem::directory_entry& entry : *cards)
        {
            // `card0` is a device; `card0-HDMI-A-1` is one of its connectors.
            const std::string name = entry.path().filename().native();
            if (!name.starts_with("card") || name.find('-') != std::string::npos)
                continue;

            if (!readWhole(entry.path() / "device" / "vendor", text, sIdFileBytesMax).isOk())
                continue;
            if (const std::optional<std::uint16_t> vendor = LinuxText::pciId(text); vendor.has_value())
                found.push_back(*vendor);
        }

        std::ranges::sort(found);
        found.erase(std::ranges::unique(found).begin(), found.end());
        return found;
    }
}
