#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

/// Text the Linux kernel writes about a process and its machine, read on every system: what reads
/// it from the kernel is Linux's own (`processposix.cpp`), and what it says is plain text a test
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
}
