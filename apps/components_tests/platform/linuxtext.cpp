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

    /// **A DRM client's memory is its `drm-total-` and `drm-resident-` sizes, in bytes**, as amdgpu writes them: a
    /// size in KiB is 1024 bytes a unit, one in MiB 1048576, one with no unit bytes; a region the file names not is
    /// nought; a file with no `drm-driver` is no client, and a size that is no number, or in a unit it does not name,
    /// is nothing.
    TEST(RtxPlatformLinuxTextTest, aDrmClientsMemoryIsItsTotalAndResidentSizesInBytes)
    {
        constexpr std::string_view handheld
            = "pos:\t0\nflags:\t02100002\ndrm-driver:\tamdgpu\ndrm-client-id:\t224\n"
              "drm-total-cpu:\t0\ndrm-resident-cpu:\t3 KiB\n"
              "drm-total-gtt:\t12304 KiB\ndrm-resident-gtt:\t26584 KiB\n"
              "drm-total-vram:\t2 MiB\ndrm-resident-vram:\t14372 KiB\n"
              "drm-memory-vram:\t14372 KiB\ndrm-memory-gtt: \t26584 KiB\n";
        EXPECT_EQ(Platform::LinuxText::driverMemory(handheld),
            (Platform::Process::DriverMemory{ .mTotalVram = 2 * 1048576,
                .mResidentVram = 14372 * 1024,
                .mTotalGtt = 12304 * 1024,
                .mResidentGtt = 26584 * 1024,
                .mResidentCpu = 3 * 1024 }));
        EXPECT_EQ(Platform::LinuxText::driverMemory("drm-driver:\tamdgpu\ndrm-total-gtt:\t5\n"),
            (Platform::Process::DriverMemory{ .mTotalGtt = 5 }));
        EXPECT_EQ(Platform::LinuxText::driverMemory("pos:\t0\nflags:\t02\n"), std::nullopt) << "a socket's";
        EXPECT_EQ(Platform::LinuxText::driverMemory("drm-driver:\tamdgpu\ndrm-total-gtt:\tmany\n"), std::nullopt);
        EXPECT_EQ(Platform::LinuxText::driverMemory("drm-driver:\tamdgpu\ndrm-total-gtt:\t5 GiB\n"), std::nullopt);
    }

    /// **What the system leaves a process is `MemAvailable` less its `VmSwap`**: 8 kB available with 3 kB of the
    /// process in swap is 5 kB, 5120 bytes; with more in swap than is available, none; with no swap line, all 8;
    /// with no `MemAvailable`, nothing. The kernel counts what it freed by swapping the process out as available,
    /// which the process takes back.
    TEST(RtxPlatformLinuxTextTest, whatTheSystemLeavesAProcessIsMemAvailableLessItsSwap)
    {
        constexpr std::string_view meminfo = "MemTotal:       65000000 kB\nMemAvailable:          8 kB\n";
        EXPECT_EQ(Platform::LinuxText::availableToProcess(meminfo, "RssAnon:\t2 kB\nVmSwap:\t       3 kB\n"),
            std::optional<std::uint64_t>(5120));
        EXPECT_EQ(Platform::LinuxText::availableToProcess(meminfo, "VmSwap:\t9 kB\n"), std::optional<std::uint64_t>(0));
        EXPECT_EQ(
            Platform::LinuxText::availableToProcess(meminfo, "RssAnon:\t2 kB\n"), std::optional<std::uint64_t>(8192));
        EXPECT_EQ(Platform::LinuxText::availableToProcess("MemTotal: 1 kB\n", "VmSwap:\t3 kB\n"), std::nullopt);
    }

    /// **The anonymous memory is `RssAnon` and `VmSwap`, in bytes**, from a status's tab-separated lines: 2 kB
    /// resident is 2048 bytes and 3 kB swapped 3072; nothing where either is missing or not in kilobytes.
    TEST(RtxPlatformLinuxTextTest, theAnonymousMemoryIsRssAnonAndVmSwapInBytes)
    {
        const std::optional<Platform::Process::AnonymousMemory> memory = Platform::LinuxText::anonymousMemory(
            "Name:\topenmw\nVmRSS:\t     9000 kB\nRssAnon:\t       2 kB\nRssFile:\t     100 kB\nVmSwap:\t       3 "
            "kB\n");
        ASSERT_TRUE(memory.has_value());
        EXPECT_EQ(memory->mResident, 2048u);
        EXPECT_EQ(memory->mSwapped, 3072u);
        EXPECT_EQ(Platform::LinuxText::anonymousMemory("RssAnon:\t2 kB\n"), std::nullopt) << "no swap line";
        EXPECT_EQ(Platform::LinuxText::anonymousMemory("RssAnon:\t2 MB\nVmSwap:\t3 kB\n"), std::nullopt);
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

    /// **A PCI id is `0x` and four hexadecimal digits, with the line break sysfs writes**: NVIDIA's
    /// `0x10de` is 4318, upper case as well, and anything else is not one: no prefix, three or five
    /// digits, a sign, a word, an empty file.
    TEST(RtxPlatformLinuxTextTest, aPciIdIsFourHexadecimalDigitsAfterItsPrefix)
    {
        EXPECT_EQ(Platform::LinuxText::pciId("0x10de\n"), std::optional<std::uint16_t>(0x10DE));
        EXPECT_EQ(Platform::LinuxText::pciId("0x10DE"), std::optional<std::uint16_t>(4318));
        EXPECT_EQ(Platform::LinuxText::pciId("0x1002\n"), std::optional<std::uint16_t>(0x1002));
        EXPECT_EQ(Platform::LinuxText::pciId("0xffff\n"), std::optional<std::uint16_t>(65535));

        for (const std::string_view text :
            { "", "\n", "10de\n", "0x10d\n", "0x10de0\n", "0x+10d", "0xgggg", "0x10de\n\n", "0X10de" })
            EXPECT_EQ(Platform::LinuxText::pciId(text), std::nullopt) << '"' << text << '"';
    }
}
