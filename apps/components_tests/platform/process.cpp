#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include <components/platform/process.hpp>

namespace
{
    using Cpus = std::optional<std::vector<std::uint32_t>>;

    /// **A CPU list is single CPUs and ranges, both ends in, with the line break sysfs writes**, and
    /// anything else is not one: an empty list, an empty item, a range backwards, a sign, a number
    /// past the kernel's 8192.
    TEST(RtxPlatformProcessTest, aCpuListNamesItsSingleCpusAndBothEndsOfItsRanges)
    {
        EXPECT_EQ(Platform::Process::parseCpuList("0-15\n"),
            (Cpus{ { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("0-2,8,16-17"), (Cpus{ { 0, 1, 2, 8, 16, 17 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("3"), (Cpus{ { 3 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("5-5"), (Cpus{ { 5 } }));
        EXPECT_EQ(Platform::Process::parseCpuList("8191"), (Cpus{ { 8191 } }));

        for (const std::string_view text :
            { "", "\n", ",", "0,", ",0", "0-", "-3", "3-1", "a", "0-x", "+1", "8192", "0-8192", "1 2" })
            EXPECT_EQ(Platform::Process::parseCpuList(text), std::nullopt) << '"' << text << '"';
    }

    /// **The share of anonymous memory on huge pages, by hand from a rollup**: 6144 kB of 8192 on
    /// transparent ones is 0.75; 2048 kB reserved beside 2048 anonymous, none of it transparent, is
    /// half; none on huge pages is nought. Nothing where there is no anonymous memory, where a
    /// field the share needs is not a number of kilobytes, or where the rollup is empty.
    TEST(RtxPlatformProcessTest, theHugePageShareIsWhatTheKernelGaveOfTheAnonymousMemory)
    {
        const auto rollup = [](std::string_view anonymous, std::string_view transparent, std::string_view reserved) {
            return "556796d45000-7fff9e1d9000 ---p 00000000 00:00 0                          [rollup]\n"
                   "Rss:                2292 kB\nAnonymous:     "
                + std::string(anonymous)
                + " kB\nKSM:                   0 kB\nAnonHugePages:  " + std::string(transparent)
                + " kB\nShmemPmdMapped:        0 kB\nPrivate_Hugetlb:  " + std::string(reserved) + " kB\n";
        };

        EXPECT_EQ(Platform::Process::hugePageShare(rollup("8192", "6144", "0")), 0.75f);
        EXPECT_EQ(Platform::Process::hugePageShare(rollup("2048", "0", "2048")), 0.5f);
        EXPECT_EQ(Platform::Process::hugePageShare(rollup("8192", "0", "0")), 0.0f);
        EXPECT_EQ(Platform::Process::hugePageShare(rollup("0", "0", "0")), std::nullopt);
        EXPECT_EQ(Platform::Process::hugePageShare(rollup("8192", "lots", "0")), std::nullopt);
        EXPECT_EQ(Platform::Process::hugePageShare(""), std::nullopt);
    }
}
