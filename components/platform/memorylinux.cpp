#include "memory.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "kernelfilelinux.hpp"
#include "linuxtext.hpp"

namespace Platform
{
    std::optional<std::uint64_t> availableMemory()
    {
        // Each file is under a page, and 64 KiB is the largest page x86-64 and arm64 have.
        std::array<char, 65536> buffer;
        const std::optional<std::string_view> meminfo = KernelFile::read("/proc/meminfo", buffer);
        if (!meminfo.has_value())
            return std::nullopt;
        std::optional<std::uint64_t> available = LinuxText::availableMemory(*meminfo);
        if (!available.has_value())
            return std::nullopt;

        // **Every cgroup from the process's own up to the root**, as each caps what the groups under it
        // hold: the room is the least any of them leaves.
        const std::optional<std::string_view> listed = KernelFile::read("/proc/self/cgroup", buffer);
        if (!listed.has_value())
            return available;
        const std::optional<std::string_view> unified = LinuxText::unifiedCgroup(*listed);
        if (!unified.has_value())
            return available;
        std::string group(*unified);
        while (!group.empty() && group != "/")
        {
            const std::string folder = "/sys/fs/cgroup" + group;
            const std::optional<std::string_view> maxText = KernelFile::read((folder + "/memory.max").c_str(), buffer);
            const std::optional<std::uint64_t> most
                = maxText.has_value() ? LinuxText::cgroupBytes(*maxText) : std::nullopt;
            if (most.has_value())
            {
                const std::optional<std::string_view> currentText
                    = KernelFile::read((folder + "/memory.current").c_str(), buffer);
                const std::optional<std::uint64_t> current
                    = currentText.has_value() ? LinuxText::cgroupBytes(*currentText) : std::nullopt;
                if (current.has_value())
                    available = std::min(*available, *most - std::min(*current, *most));
            }
            group.resize(group.find_last_of('/'));
        }
        return available;
    }
}
