#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/linuxtext.hpp>

namespace
{
    using Cpus = std::optional<std::vector<std::uint32_t>>;

    /// **A CPU list is single CPUs and ranges, both ends in, with the line break sysfs writes**, and
    /// anything else is not one: an empty list, an empty item, a range backwards, a sign, a number
    /// past the kernel's 8192.
    TEST(RtxPlatformLinuxTextTest, aCpuListNamesItsSingleCpusAndBothEndsOfItsRanges)
    {
        EXPECT_EQ(Platform::LinuxText::parseCpuList("0-15\n"),
            (Cpus{ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } }));
        EXPECT_EQ(Platform::LinuxText::parseCpuList("0-2,8,16-17"), (Cpus{ { 0, 1, 2, 8, 16, 17 } }));
        EXPECT_EQ(Platform::LinuxText::parseCpuList("3"), (Cpus{ { 3 } }));
        EXPECT_EQ(Platform::LinuxText::parseCpuList("5-5"), (Cpus{ { 5 } }));
        EXPECT_EQ(Platform::LinuxText::parseCpuList("8191"), (Cpus{ { 8191 } }));

        for (const std::string_view text :
            { "", "\n", ",", "0,", ",0", "0-", "-3", "3-1", "a", "0-x", "+1", "8192", "0-8192", "1 2" })
            EXPECT_EQ(Platform::LinuxText::parseCpuList(text), std::nullopt) << '"' << text << '"';
    }

    /// **The share of anonymous memory on huge pages, by hand from a rollup**: 6144 kB of 8192 on
    /// transparent ones is 0.75; 2048 kB reserved beside 2048 anonymous, none of it transparent, is
    /// half; none on huge pages is nought. Nothing where there is no anonymous memory, where a
    /// field the share needs is not a number of kilobytes, or where the rollup is empty.
    TEST(RtxPlatformLinuxTextTest, theHugePageShareIsWhatTheKernelGaveOfTheAnonymousMemory)
    {
        const auto rollup = [](std::string_view anonymous, std::string_view transparent, std::string_view reserved) {
            return "556796d45000-7fff9e1d9000 ---p 00000000 00:00 0                          [rollup]\n"
                   "Rss:                2292 kB\nAnonymous:     "
                + std::string(anonymous)
                + " kB\nKSM:                   0 kB\nAnonHugePages:  " + std::string(transparent)
                + " kB\nShmemPmdMapped:        0 kB\nPrivate_Hugetlb:  " + std::string(reserved) + " kB\n";
        };

        EXPECT_EQ(Platform::LinuxText::hugePageShare(rollup("8192", "6144", "0")), 0.75f);
        EXPECT_EQ(Platform::LinuxText::hugePageShare(rollup("2048", "0", "2048")), 0.5f);
        EXPECT_EQ(Platform::LinuxText::hugePageShare(rollup("8192", "0", "0")), 0.0f);
        EXPECT_EQ(Platform::LinuxText::hugePageShare(rollup("0", "0", "0")), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::hugePageShare(rollup("8192", "lots", "0")), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::hugePageShare(""), std::nullopt);
    }

    /// **The compaction stalls are the `compact_stall` counter's**, among the kernel's others: 42 where it
    /// says 42, and nothing where it names none or its value is not a number.
    TEST(RtxPlatformLinuxTextTest, theCompactionStallsAreTheKernelsCounter)
    {
        EXPECT_EQ(
            Platform::LinuxText::compactionStalls("compact_migrate_scanned 9\ncompact_stall 42\ncompact_fail 1\n"),
            std::optional<std::uint64_t>(42));
        EXPECT_EQ(Platform::LinuxText::compactionStalls("compact_stall 7"), std::optional<std::uint64_t>(7));
        EXPECT_EQ(Platform::LinuxText::compactionStalls("compact_fail 1\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::compactionStalls("compact_stall many\n"), std::nullopt);
    }

    /// **The available memory is `MemAvailable`, in bytes**: 2 kB is 2048 bytes among the other fields,
    /// and nothing where the field is missing, its value no number, or its unit not kilobytes.
    TEST(RtxPlatformLinuxTextTest, theAvailableMemoryIsMemAvailableInBytes)
    {
        EXPECT_EQ(Platform::LinuxText::availableMemory("MemTotal:       65000000 kB\nMemFree:          100 kB\n"
                                                       "MemAvailable:          2 kB\nBuffers:            1 kB\n"),
            std::optional<std::uint64_t>(2048));
        EXPECT_EQ(Platform::LinuxText::availableMemory("MemTotal:       65000000 kB\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::availableMemory("MemAvailable: many kB\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::availableMemory("MemAvailable: 2 MB\n"), std::nullopt);
    }

    /// **The cgroup is the v2 line's path, and its bytes a number or no limit**: `/user.slice/app` from a
    /// list with a v1 line before it, the root as `/`, and nothing where no `0::` line names a path; a
    /// limit of 1073741824 with its line break, nothing for `max`, an empty file or a word.
    TEST(RtxPlatformLinuxTextTest, theCgroupIsTheUnifiedLinesPathAndItsBytesANumber)
    {
        EXPECT_EQ(Platform::LinuxText::unifiedCgroup("1:name=systemd:/x\n0::/user.slice/app\n"),
            std::optional<std::string_view>("/user.slice/app"));
        EXPECT_EQ(Platform::LinuxText::unifiedCgroup("0::/\n"), std::optional<std::string_view>("/"));
        EXPECT_EQ(Platform::LinuxText::unifiedCgroup("1:name=systemd:/x\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::unifiedCgroup("0::\n"), std::nullopt);

        EXPECT_EQ(Platform::LinuxText::cgroupBytes("1073741824\n"), std::optional<std::uint64_t>(1073741824));
        EXPECT_EQ(Platform::LinuxText::cgroupBytes("max\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::cgroupBytes(""), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::cgroupBytes("12 bytes\n"), std::nullopt);
    }
}
