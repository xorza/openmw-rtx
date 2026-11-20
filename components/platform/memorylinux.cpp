#include "memory.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
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
        std::array<char, 65536> buffer{};
        std::array<char, 65536> statusBuffer{};
        const std::optional<std::string_view> meminfo = KernelFile::read("/proc/meminfo", buffer);
        if (!meminfo.has_value())
            return std::nullopt;
        const std::optional<std::string_view> status = KernelFile::read("/proc/self/status", statusBuffer);
        std::optional<std::uint64_t> available
            = LinuxText::availableToProcess(*meminfo, status.has_value() ? *status : std::string_view());
        if (!available.has_value())
            return std::nullopt;

        // **Every cgroup from the process's own up to the root**, as each caps what the groups under it
        // hold: the room is the least any of them leaves, under the lower of its two limits and with its pages
        // of files counted as room (`LinuxText::cgroupRoom`).
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
            const auto bytesOf = [&](const char* const file) -> std::optional<std::uint64_t> {
                const std::optional<std::string_view> text = KernelFile::read((folder + file).c_str(), buffer);
                return text.has_value() ? LinuxText::cgroupBytes(*text) : std::nullopt;
            };
            const std::optional<std::uint64_t> most = bytesOf("/memory.max");
            const std::optional<std::uint64_t> high = bytesOf("/memory.high");
            const std::optional<std::uint64_t> current = bytesOf("/memory.current");
            if ((most.has_value() || high.has_value()) && current.has_value())
            {
                constexpr std::uint64_t unlimited = std::numeric_limits<std::uint64_t>::max();
                const std::optional<std::string_view> stat
                    = KernelFile::read((folder + "/memory.stat").c_str(), buffer);
                const std::uint64_t fileBytes = stat.has_value() ? LinuxText::cgroupFileBytes(*stat) : 0;
                available = std::min(*available,
                    LinuxText::cgroupRoom(
                        std::min(most.value_or(unlimited), high.value_or(unlimited)), *current, fileBytes));
            }
            group.resize(group.find_last_of('/'));
        }
        return available;
    }
}
