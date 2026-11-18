#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "process.hpp"

/// Text the Linux kernel writes about a process and its machine, read on every system: what reads
/// it from the kernel is Linux's own (`processlinux.cpp`), and what it says is plain text a test
/// on any machine can hand it.
namespace Platform::LinuxText
{
    /// The share of a process's anonymous memory that stands on huge pages, from the memory rollup
    /// as `/proc/<pid>/smaps_rollup` writes it: transparent ones (`AnonHugePages`) over `Anonymous`,
    /// and reserved ones (`Private_Hugetlb`), which `Anonymous` leaves out, on both sides. **What
    /// the kernel gave, and not what was asked**: a tunable glibc does not know, a mode of `never`
    /// or memory too broken up for a huge page all leave the share low. Nothing where the rollup
    /// names no anonymous memory, or a field is not a number of kilobytes.
    std::optional<float> hugePageShare(std::string_view rollup);

    /// The CPUs a Linux CPU list names — `0-7,16-19`, as sysfs writes it with its line break — in
    /// the list's order, or nothing where the text is not one. A number past the kernel's own limit
    /// of 8192 CPUs is not one either, so a range cannot ask for billions of entries.
    std::optional<std::vector<std::uint32_t>> parseCpuList(std::string_view text);

    /// How many times the kernel stalled an allocation to compact memory, from its counters as
    /// `/proc/vmstat` writes them (`compact_stall`), or nothing where they name none.
    std::optional<std::uint64_t> compactionStalls(std::string_view vmstat);

    /// The bytes the kernel estimates it can hand out without swapping, from `/proc/meminfo`
    /// (`MemAvailable`, in kilobytes), or nothing where it names none.
    std::optional<std::uint64_t> availableMemory(std::string_view meminfo);

    /// What the system leaves a process, from `/proc/meminfo` and the process's status: `MemAvailable` less what of
    /// the process is in swap (`VmSwap`), which it is the process's to take back, and which the kernel counts as
    /// available once it freed the memory by swapping it out. `MemAvailable` alone where the status names no swap;
    /// nothing where the meminfo names no `MemAvailable`.
    std::optional<std::uint64_t> availableToProcess(std::string_view meminfo, std::string_view status);

    /// What a process holds of anonymous memory, from its status as `/proc/<pid>/status` writes it: resident
    /// (`RssAnon`) and swapped out (`VmSwap`), in bytes, or nothing where either field is missing or not a number of
    /// kilobytes.
    std::optional<Process::AnonymousMemory> anonymousMemory(std::string_view status);

    /// The process's cgroup v2, from `/proc/self/cgroup` (its `0::` line), as a path under the cgroup
    /// file system's root, `/` for the root itself; nothing where it is in no v2 group.
    std::optional<std::string_view> unifiedCgroup(std::string_view cgroup);

    /// The bytes a cgroup v2 file states, `memory.max` or `memory.current`, one number and a line
    /// break: nothing where it says `max`, no limit, or is not a number.
    std::optional<std::uint64_t> cgroupBytes(std::string_view text);

    /// One DRM client as one of its open files states it: the kernel's number for the client, `drm-client-id`, which
    /// every file that shares or duplicates it states too, and what it holds.
    struct DriverClient
    {
        std::uint64_t mId = 0;
        Process::DriverMemory mMemory;

        bool operator==(const DriverClient& other) const = default;
    };

    /// The DRM client an open file is, from its information as `/proc/<pid>/fdinfo/<fd>` writes it
    /// (`drm-usage-stats`): its `drm-client-id`, and its `drm-total-` and `drm-resident-` sizes for `vram` and `gtt`
    /// and `drm-resident-cpu`, each a number with no unit, `KiB` or `MiB`. Nothing where the file names no
    /// `drm-driver`, a file that is no DRM client, or no `drm-client-id`, which tells a client from its duplicates; a
    /// region it does not name is nought, and a size or an id that is not a number is nothing.
    std::optional<DriverClient> driverClient(std::string_view fdinfo);

    /// The PCI id a device's sysfs file states, `vendor` or `device`: four hexadecimal digits after
    /// `0x` and a line break, as the kernel writes them, or nothing where the text is not one.
    std::optional<std::uint16_t> pciId(std::string_view text);
}
